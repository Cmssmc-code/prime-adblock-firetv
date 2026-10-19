// inflate_hook.cpp — see inflate_hook.h.
//
// libignite.so 6.23.23 armeabi-v7a (sha256 0742379155072cf7…): inflate() at +0xd331a0,
//   d331a0  f0b5      push {r4-r7, lr}
//   d331a2  03af      add  r7, sp, #0xc
//   d331a4  2de9000f  push.w {r8, sb, sl, fp}        <- 8 bytes, position independent
//   d331a8  99b0      sub  sp, #0x64
// We overwrite those 8 bytes with `ldr.w pc, [pc, #0]` + the proxy address, and the
// trampoline below replays them before jumping to inflate()+8.
#include "inflate_hook.h"
#include "rego_stream.h"
#include "evlog.h"

#include <android/log.h>
#include <cstdio>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PVNativeHook", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "PVNativeHook", __VA_ARGS__)

extern "C" {
__attribute__((visibility("hidden"))) volatile uintptr_t g_pv_inflate_cont = 0;
__attribute__((visibility("hidden"))) int pv_inflate_tramp(void* strm, int flush);
}

__asm__(
    ".text\n"
    ".thumb\n"
    ".balign 4\n"
    ".hidden pv_inflate_tramp\n"
    ".globl pv_inflate_tramp\n"
    ".type pv_inflate_tramp,%function\n"
    ".thumb_func\n"
    "pv_inflate_tramp:\n"
    "    push {r4, r5, r6, r7, lr}\n"
    "    add r7, sp, #0xc\n"
    "    push.w {r8, sb, sl, fp}\n"
    "    ldr.w ip, 2f\n"
    "1:  add ip, pc\n"
    "    ldr ip, [ip]\n"
    "    bx ip\n"
    ".balign 4\n"
    "2:  .word g_pv_inflate_cont - (1b + 4)\n");

namespace pvinfl {
namespace {

// Known libignite.so builds: GNU build-id -> file offset of inflate() and its first 8 bytes
// (a position-independent prologue the patch overwrites). To add a build, see docs/PORTING.md.
struct Build { const char* id; uintptr_t off; unsigned char prologue[8]; };
constexpr Build kBuilds[] = {
    // Prime Video 6.23.23 (Fire TV, armeabi-v7a)
    {"1f2df07ac3a6468b", 0xd331a0, {0xf0, 0xb5, 0x03, 0xaf, 0x2d, 0xe9, 0x00, 0x0f}},
};

// z_stream (32-bit): next_in@0 avail_in@4 total_in@8 next_out@12 avail_out@16 total_out@20
struct ZS { const uint8_t* next_in; uint32_t avail_in, total_in; uint8_t* next_out; uint32_t avail_out, total_out; };

using inflate_fn = int (*)(void*, int);
inflate_fn g_real = nullptr;
std::atomic<bool> g_live{false};

// One tracked ad-response stream per z_stream (few run at once).
struct Entry {
    const void* strm = nullptr;
    int match = 0;                 // bytes of "playlist":[ matched so far (SEARCH)
    bool blanking = false;
    pvstream::Scan scan;
    size_t blanked = 0;
    int chunks = 0;
};
constexpr int kMax = 8;
Entry g_e[kMax];
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct Lock {
    Lock()  { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};
std::atomic<int> g_diag{0};
std::atomic<uint64_t> g_gated{0}, g_done{0}, g_dropped{0};

constexpr char kPlaylist[] = "\"playlist\":[";   // 12 chars

Entry* find(const void* s) { for (auto& e : g_e) if (e.strm == s) return &e; return nullptr; }
Entry* alloc(const void* s) {
    for (auto& e : g_e) if (e.strm == nullptr) { e = Entry{}; e.strm = s; return &e; }
    return nullptr;
}

const void* memfind(const char* h, size_t n, const char* needle, size_t m) {
    return n >= m ? memmem(h, n, needle, m) : nullptr;
}

// Feed the n bytes inflate just produced at `out`. total_before = bytes produced earlier.
void on_output(const void* strm, uint8_t* out, size_t n, uint32_t total_before, bool stream_end) {
    Lock l;
    Entry* e = find(strm);
    if (total_before == 0) {                              // a new body starts on this z_stream
        if (e) { e->strm = nullptr; e = nullptr; }
        const char* c = reinterpret_cast<const char*>(out);
        if (n >= 64 && c[0] == '{' && g_diag.load(std::memory_order_relaxed) < 40 &&
            (memfind(c, n, "\"playlist\"", 10) || memfind(c, n, "adDeliverySessionId", 19) ||
             memfind(c, n, "intraTitlePlaylist", 18))) {
            g_diag.fetch_add(1, std::memory_order_relaxed);
            char head[81]; size_t hl = n < 80 ? n : 80;
            for (size_t i = 0; i < hl; ++i) head[i] = (c[i] >= 32 && c[i] < 127) ? c[i] : '.';
            head[hl] = 0;
            if (memfind(c, n, "\"playlist\"", 10) && !memfind(c, n, "adDeliverySessionId", 19))
                pvev::log("AD-UNGATED playlist-without-session n=%zu head=[%.60s]", n, head);   // format we do not cover
            LOGI("PVINFL diag first chunk n=%zu pl=%d ads=%d prs=%d head=[%s]", n,
                 memfind(c, n, "\"playlist\"", 10) != nullptr, memfind(c, n, "adDeliverySessionId", 19) != nullptr,
                 memfind(c, n, "intraTitlePlaylist", 18) != nullptr, head);
        }
        if (n < 64 || c[0] != '{' ||
            !memfind(c, n, "\"adDeliverySessionId\"", 21) ||
            memfind(c, n, "intraTitlePlaylist", 18)) return;
        e = alloc(strm);
        if (!e) return;
        g_gated.fetch_add(1, std::memory_order_relaxed);
        LOGI("PVINFL gated ad response, first chunk n=%zu", n);
        pvev::log("AD-GATED first=%zu", n);
    }
    if (!e) return;
    char* c = reinterpret_cast<char*>(out);
    size_t i = 0;
    if (!e->blanking) {                                   // look for "playlist":[ (may straddle chunks)
        for (; i < n; ++i) {
            if (c[i] == kPlaylist[e->match]) {
                if (++e->match == 12) { e->blanking = true; e->scan = pvstream::Scan{}; ++i; break; }
            } else {
                e->match = (c[i] == kPlaylist[0]) ? 1 : 0;
            }
        }
    }
    if (e->blanking) {
        size_t end = pvstream::scan_blank(c + i, n - i, e->scan, true);
        e->blanked += end;
        ++e->chunks;
        if (end < n - i) {                                // closing ']' reached: done
            g_done.fetch_add(1, std::memory_order_relaxed);
            LOGI("PVKILL path=tv-inflate chunks=%d blanked=%zu", e->chunks, e->blanked);
            pvev::log("AD-KILLED chunks=%d bytes=%zu", e->chunks, e->blanked);
            e->strm = nullptr;
            return;
        }
    }
    if (stream_end) {
        if (e->blanking) LOGW("PVINFL stream ended inside the playlist array (blanked=%zu)", e->blanked);
        else             LOGW("PVINFL stream ended without a playlist array");
        pvev::log("AD-MISSED reason=%s bytes=%zu", e->blanking ? "ended-inside-array" : "no-array", e->blanked);
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        e->strm = nullptr;
    }
}

int proxy_inflate(void* strm, int flush) {
    ZS* z = static_cast<ZS*>(strm);
    uint8_t* before = z ? z->next_out : nullptr;
    int r = g_real(strm, flush);
    if (z && before && z->next_out >= before) {
        size_t n = static_cast<size_t>(z->next_out - before);
        if (n > 0) on_output(strm, before, n, z->total_out - static_cast<uint32_t>(n), r == 1 /*Z_STREAM_END*/);
    }
    return r;
}

struct FindCtx { const char* sub; uintptr_t base; bool found; char build_id[65]; };
int iter_cb(struct dl_phdr_info* info, size_t, void* data) {
    auto* c = static_cast<FindCtx*>(data);
    if (info->dlpi_name && strstr(info->dlpi_name, c->sub)) {
        c->base = info->dlpi_addr; c->found = true; c->build_id[0] = 0;
        for (int i = 0; i < info->dlpi_phnum; ++i) {                 // GNU build-id from PT_NOTE
            const ElfW(Phdr)& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_NOTE) continue;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
            const uint8_t* end = p + ph.p_memsz;
            while (p + 12 <= end) {
                uint32_t namesz = *reinterpret_cast<const uint32_t*>(p), descsz = *reinterpret_cast<const uint32_t*>(p + 4),
                         type = *reinterpret_cast<const uint32_t*>(p + 8);
                const uint8_t* name = p + 12; const uint8_t* desc = name + ((namesz + 3) & ~3u);
                if (type == 3 && namesz == 4 && memcmp(name, "GNU", 4) == 0 && descsz <= 32) {
                    for (uint32_t k = 0; k < descsz; ++k) snprintf(c->build_id + 2 * k, 3, "%02x", desc[k]);
                    break;
                }
                p = desc + ((descsz + 3) & ~3u);
            }
        }
        return 1;
    }
    return 0;
}

}  // namespace

bool install(const char* lib_substr) {
    if (g_live.load()) return true;
    FindCtx ctx{lib_substr, 0, false, {0}};
    dl_iterate_phdr(iter_cb, &ctx);
    if (!ctx.found) return false;
    const Build* build = nullptr;
    for (const Build& bd : kBuilds) if (strcmp(bd.id, ctx.build_id) == 0) build = &bd;
    if (!build) {
        LOGW("PVINFL unknown libignite build-id '%s' — hook skipped", ctx.build_id);
        pvev::log("HOOK-OFF reason=unknown-build id=%s (Prime updated? see docs/PORTING.md)", ctx.build_id);
        return false;
    }
    const uintptr_t kInflateOffset = build->off;
    unsigned char* fn = reinterpret_cast<unsigned char*>(ctx.base + kInflateOffset);
    if (memcmp(fn, build->prologue, 8) != 0) {
        LOGW("PVINFL inflate prologue mismatch at libignite+0x%zx — hook skipped", (size_t)kInflateOffset);
        pvev::log("HOOK-OFF reason=prologue-mismatch id=%s", ctx.build_id);
        return false;
    }
    g_real = reinterpret_cast<inflate_fn>(&pv_inflate_tramp);
    g_pv_inflate_cont = reinterpret_cast<uintptr_t>(fn + 8) | 1u;

    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page = reinterpret_cast<uintptr_t>(fn) & ~(static_cast<uintptr_t>(ps) - 1);
    size_t span = (reinterpret_cast<uintptr_t>(fn) + 8 > page + ps) ? 2 * ps : ps;
    // RWX in one step: other threads may be executing in this page right now.
    if (mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGW("PVINFL mprotect RWX failed (errno=%d) — inline hook impossible on this ROM", errno);
        pvev::log("HOOK-OFF reason=mprotect errno=%d", errno);
        return false;
    }
    uint32_t proxy = reinterpret_cast<uintptr_t>(&proxy_inflate) | 1u;
    const unsigned char ldr_pc[4] = {0xdf, 0xf8, 0x00, 0xf0};      // ldr.w pc, [pc, #0]
    memcpy(fn + 4, &proxy, 4);                                       // literal first
    memcpy(fn, ldr_pc, 4);                                           // then the jump
    __builtin___clear_cache(reinterpret_cast<char*>(fn), reinterpret_cast<char*>(fn + 8));
    mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_EXEC);
    g_live.store(true);
    LOGI("PVINFL inflate hooked at libignite+0x%zx (proxy=%p)", (size_t)kInflateOffset, (void*)&proxy_inflate);
    pvev::log("HOOK-ON inflate=libignite+0x%zx", (size_t)kInflateOffset);
    pvev::install_crash_handler(ctx.base);
    return true;
}

}  // namespace pvinfl
