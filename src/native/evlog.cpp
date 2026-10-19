// evlog.cpp — see evlog.h.
#include "evlog.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

namespace pvev {
namespace {

constexpr off_t kRotateAt = 1 << 20;
int g_fd = -1;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
uintptr_t g_ignite_base = 0;
uintptr_t g_self_base = 0;
struct sigaction g_old[32];
const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

void mkdirs(char* path) {
    for (char* p = path + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(path, 0770); *p = '/'; }
    }
    mkdir(path, 0770);
}

int stamp(char* out, size_t n) {
    timeval tv; gettimeofday(&tv, nullptr);
    tm t; localtime_r(&tv.tv_sec, &t);
    return snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d pid=%d ", t.tm_year + 1900, t.tm_mon + 1,
                    t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (int)(tv.tv_usec / 1000), (int)getpid());
}

bool in_range(unsigned long a, uintptr_t base, unsigned long size) {
    return base && a >= base && a < base + size;
}

void crash_handler(int sig, siginfo_t* si, void* uctx) {
    auto* uc = static_cast<ucontext_t*>(uctx);
    unsigned long pc = 0, lr = 0;
#if defined(__arm__)
    pc = uc->uc_mcontext.arm_pc; lr = uc->uc_mcontext.arm_lr;
#endif
    // ART raises SIGSEGV on purpose (implicit null checks, stack probes) from managed code: only
    // faults whose pc/lr sit in libignite (15 MB) or in this library are ours to record.
    bool ours = in_range(pc, g_ignite_base, 0x1000000) || in_range(lr, g_ignite_base, 0x1000000) ||
                in_range(pc, g_self_base, 0x100000) || in_range(lr, g_self_base, 0x100000);
    if (ours || sig == SIGABRT) {
        char b[256];
        int n = stamp(b, sizeof b);
        n += snprintf(b + n, sizeof b - n,
                      "CRASH sig=%d code=%d fault=%p pc=0x%lx lr=0x%lx ignite=0x%lx pvhook=0x%lx tid=%d in=%s\n", sig,
                      si ? si->si_code : 0, si ? si->si_addr : nullptr, pc, lr, (unsigned long)g_ignite_base,
                      (unsigned long)g_self_base, (int)gettid(), ours ? "libignite/pvhook" : "other");
        if (g_fd >= 0) { ssize_t r = write(g_fd, b, n); (void)r; fsync(g_fd); }
    }
    // Chain to whoever was installed before us (ART, debuggerd). We stay installed.
    const struct sigaction& o = g_old[sig];
    if (o.sa_flags & SA_SIGINFO) {
        if (o.sa_sigaction) { o.sa_sigaction(sig, si, uctx); return; }
    } else if (o.sa_handler != SIG_DFL && o.sa_handler != SIG_IGN) {
        o.sa_handler(sig); return;
    }
    // default action: reinstall it and re-deliver
    signal(sig, SIG_DFL);
    if (si && si->si_code <= 0) raise(sig);       // synchronous faults re-fault on return
}

}  // namespace

void init() {
    if (g_lock.test_and_set()) return;            // already initialised
    char cmd[128] = {0};
    int f = open("/proc/self/cmdline", O_RDONLY);
    if (f >= 0) { ssize_t r = read(f, cmd, sizeof cmd - 1); (void)r; close(f); }
    char dir[256];
    snprintf(dir, sizeof dir, "/storage/emulated/0/Android/data/%s/files", cmd);
    mkdirs(dir);
    char path[300], old[310];
    snprintf(path, sizeof path, "%s/pvhook_events.log", dir);
    snprintf(old, sizeof old, "%s.1", path);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > kRotateAt) rename(path, old);
    g_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    log("START pkg=%s pvhook=events-1", cmd);
}

void log(const char* fmt, ...) {
    if (g_fd < 0) return;
    char b[512];
    int n = stamp(b, sizeof b);
    va_list ap; va_start(ap, fmt);
    n += vsnprintf(b + n, sizeof b - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof b - 2) n = sizeof b - 2;
    b[n++] = '\n';
    ssize_t r = write(g_fd, b, n); (void)r;
}

void install_crash_handler(uintptr_t ignite_base) {
    g_ignite_base = ignite_base;
    Dl_info di;
    if (dladdr(reinterpret_cast<void*>(&crash_handler), &di) && di.dli_fbase)
        g_self_base = reinterpret_cast<uintptr_t>(di.dli_fbase);
    for (int s : kSignals) {
        struct sigaction sa{};
        sa.sa_sigaction = crash_handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(s, &sa, &g_old[s]);
    }
    log("crash handler installed ignite=0x%lx", (unsigned long)ignite_base);
}

}  // namespace pvev
