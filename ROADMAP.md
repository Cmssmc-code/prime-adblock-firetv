# Roadmap

Where help is most useful, roughly in order of value. Items link to the matching issues once they are opened.

## Make it trustworthy

1. **Field-test `main`** (event log, crash recorder, build-id table). It compiles but has not run on a device yet.
2. **Device reports.** Fire TV Stick 4K (2nd gen), Fire TV Cube, Fire TV Stick Lite, Onn 4K, Chromecast with Google TV, Shield.
   Which ones run 6.23.23 armv7a and does the hook install (`HOOK-ON`) there?
3. **A week-long log** from several users: success rate (`AD-KILLED / AD-GATED`), anything in `AD-MISSED`, `AD-UNGATED`, `CRASH`.

## Make it portable

4. **Other Prime builds.** Add `kBuilds[]` entries (see `docs/PORTING.md`). Even a negative result ("6.24.x: no zlib strings in
   libignite.so") helps.
5. **Find `inflate()` at runtime** instead of by table: fingerprint the function from its code (state-machine jump table,
   the three error strings) so new builds work without a new release.
6. **arm64.** A64 trampoline (`ldr x16,#8; br x16` + literal, with the usual veneer rules), and the same prologue verification.

## Make it robust

7. **Host-side tests for the `inflate` stream scanner.** The "find `"playlist":[` across chunk boundaries, then blank to the matching
   `]`" logic in `inflate_hook.cpp` is only exercised on a device. Move it to a header (like `rego_stream.h`) and test every split point.
8. **Mid-roll edge cases.** Very large breaks, scrubbing during a fetch, resume after a crash: anything where an ad slipped through
   or the player stalled. Reproduce, then capture the `events.log` and `logcat`.
9. **Other ad containers.** `AD-UNGATED` marks a response that mentions a playlist but is not the format we handle. If you see one, the
   head of the response (60 bytes) is logged: that is the starting point for supporting it.

## Make it easy

10. **One-command install**: a script that takes the user's Morphe-patched APK and a keystore and does everything including `adb install`.
11. **A GitHub Action** that builds a release asset (the library, with SHA-256) on tags.
12. **Translations** of the README (French is in; Spanish, German, Portuguese welcome).

## Out of scope

Redistributing any Amazon binary; features that need root; DRM circumvention; anything beyond ads in services you can already watch.
