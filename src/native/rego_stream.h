// rego_stream.h — follow a TRUNCATED regolith getVideoAds response across the
// chunks libcurl delivers after the first one, and empty its "playlist" array
// once the rest of the body arrives (#120 mid-roll / pre-roll ads that played).
//
// Why: maybe_empty_regolith() only sees the first assembled chunk. When the
// "playlist":[ array does not close inside it, Layer 1 can only blank the
// complete ad objects, and the ads in the tail still play (16 of 22 ad breaks
// in the 2026-10-15..19 log). The old stitcher looked for the next chunk at
// dst == base + len and expired every time: the app's body buffer moves when it
// grows (realloc, or new + full copy), so the follow-up chunk lands next to a
// NEW base.
//
// This header is the pure part (no locking, no logging, host-testable):
//   Scan / scan_blank()   string-aware walk that blanks the array interior up
//                         to, not including, the ']' that closes it.
//   Stream                one followed response: where its buffer is now, how
//                         much of it is filled, and the JSON state at the end.
//   on_chunk1()           arm after Layer 1 ran on the truncated first chunk.
//   on_copy()             every later copy: a full copy of the buffer = moved;
//                         a copy landing at base + len = next chunk -> blank.
//   on_realloc()          realloc of the buffer = moved.
// Result once the closing ']' arrives: "playlist":[<spaces>] — the same valid,
// same-length empty array as the complete-response path, so the app takes its
// own clean empty-break path. Until then the buffer stays valid JSON (Layer 1
// output), so a stream we lose track of behaves exactly as before.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pvstream {

struct Scan {
    int depth = 1;        // 1 = directly inside the playlist array
    bool in_str = false;
    bool esc = false;
};

// Walk buf[0, n) from state `s`. When `apply`, every byte that belongs to the
// array interior becomes a space. Returns the index of the closing ']' (left
// untouched) or n if the array is still open at the end of the buffer.
inline size_t scan_blank(char* buf, size_t n, Scan& s, bool apply) {
    for (size_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (s.in_str) {
            if (s.esc) s.esc = false;
            else if (c == '\\') s.esc = true;
            else if (c == '"') s.in_str = false;
        } else if (c == '"') {
            s.in_str = true;
        } else if (c == '[' || c == '{') {
            ++s.depth;
        } else if (c == ']' || c == '}') {
            if (--s.depth == 0) return i;
        }
        if (apply) buf[i] = ' ';
    }
    return n;
}

enum class Event { None, Moved, Chunk, Done };

struct Stream {
    bool active = false;
    char* base = nullptr;     // current address of the app's body buffer
    size_t len = 0;           // bytes of the body known to be in the buffer
    size_t open = 0;          // index of the playlist '[' in the body
    Scan scan;                // JSON state at base + len (original bytes)
    int chunks = 0;           // follow-up chunks seen
    size_t blanked = 0;       // bytes blanked by the stream (not Layer 1)
};

// Arm after Layer 1 handled the truncated chunk buf[0, n) whose playlist '['
// is at `open`. Computes the JSON state at the end of the chunk. Nothing is
// written here: Layer 1 already left the chunk valid.
inline void on_chunk1(Stream& st, char* buf, size_t n, size_t open) {
    st = Stream{};
    Scan s;
    size_t end = scan_blank(buf + open + 1, n - open - 1, s, false);
    if (end != n - open - 1) return;      // closes after all: not our case
    st.active = true;
    st.base = buf;
    st.len = n;
    st.open = open;
    st.scan = s;
}

// A copy of n bytes from src to dst just happened (dst holds the bytes).
inline Event on_copy(Stream& st, char* dst, const char* src, size_t n, bool apply) {
    if (!st.active || n == 0) return Event::None;
    if (src == st.base && n >= st.len && dst != st.base) {   // whole buffer copied: moved
        st.base = dst;
        return Event::Moved;
    }
    if (dst != st.base + st.len) return Event::None;
    // Next chunk of the body, appended right after what we have. The buffer is
    // live at [base, base + len + n) — the app just wrote its end — so the
    // incomplete tail left by Layer 1 can be emptied now, then the new bytes.
    if (st.chunks == 0) {
        for (size_t i = st.open + 1; i < st.len; ++i)
            if (st.base[i] != ' ') { if (apply) st.base[i] = ' '; ++st.blanked; }
    }
    ++st.chunks;
    size_t end = scan_blank(dst, n, st.scan, apply);
    st.blanked += end;
    st.len += n;
    if (end < n) { st.active = false; return Event::Done; }
    return Event::Chunk;
}

inline Event on_realloc(Stream& st, void* old_ptr, void* new_ptr) {
    if (!st.active || old_ptr == nullptr || old_ptr != st.base) return Event::None;
    if (new_ptr == nullptr) return Event::None;           // failed realloc: old block still valid
    st.base = static_cast<char*>(new_ptr);
    return Event::Moved;
}

// free() of the buffer before the array closed: stop, never touch it again.
inline bool on_free(Stream& st, void* p) {
    if (!st.active || p != st.base) return false;
    st.active = false;
    return true;
}

} // namespace pvstream
