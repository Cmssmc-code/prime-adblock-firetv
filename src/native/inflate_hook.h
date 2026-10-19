// inflate_hook.h — inline hook on zlib's inflate() statically linked into libignite.so.
//
// Why: the getVideoAds response is gzip-decoded by libcurl (statically linked in
// libignite) in several inflate() calls. The decoded bytes reach the app through
// buffers no GOT hook can see (see docs/HISTORIQUE.md, 2026-10-19). inflate() is
// the one place where every decoded byte of every response passes, in order, so
// we blank the ad "playlist" array right there, in the bytes inflate just wrote
// to strm->next_out. zlib keeps its own copy of the history (window) and has
// already updated its checksum when inflate() returns, so editing the output
// afterwards cannot corrupt the stream.
//
// The function is stripped: it is located by file offset for ONE build of
// libignite.so (SHA-256 prefix below) and the prologue bytes are verified before
// anything is patched. Any other build: no patch, a log line, nothing else.
#pragma once

namespace pvinfl {

// Patch inflate() of the already-mapped library whose path contains `lib_substr`.
// Returns true when the hook is live. Safe to call repeatedly (no-op once live).
bool install(const char* lib_substr);

}  // namespace pvinfl
