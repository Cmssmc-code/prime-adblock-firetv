# Reverse-engineering notes: Prime Video for Android TV (6.23.23, engine v15.5)

Everything below is our own analysis of an app we run on our own device. It contains facts, offsets and short
excerpts only, no Amazon code. Where an idea comes from the upstream Morphe project it is marked *(upstream)*.
Each finding can be reproduced with `unzip`, `strings`, `llvm-readelf`, `adb logcat` and Ghidra, or with the
scripts in `tools/`.

## 1. The app

* Package `com.amazon.amazonvideo.livingroom` (Morphe renames its patched copy to `....livingroom.mod`).
  Version `6.23.23+v15.5.0.70-armv7a`. The "armv7a" build is 32-bit only, which is why every native hook here is
  ARM32 / Thumb-2. Version 6.24.x (engine v16) moved the ad pipeline into the native engine *(upstream, issue #120)*:
  this project does not apply there.
* Native libraries in `lib/armeabi-v7a/`: `libignite.so` (15.7 MB, the player core), `libignite_allocator.so`,
  `libignite-android-support.so`, `libignite-unwind.so`, `libprime-video-device-layer.so`, `libc++_shared.so`, and
  `libpvhook.so` once patched (Morphe's loader; we replace it with ours).
* `libignite.so` statically links **BoringSSL**, **libcurl** (strings such as `Got HTTP failure 417 while sending data`
  and `[%d] PUSH_PROMISE received`) and **zlib** (the inflate error strings `incorrect header check`,
  `unknown compression method`, `incorrect data check`, `invalid distance too far back`...). It is stripped: no exported
  `inflate`, no symbols. Its GNU build-id for 6.23.23 is `1f2df07ac3a6468b`.
* The player logic is JavaScript running inside the native engine; the bundle is downloaded gzip-compressed *(upstream)*.
  Hooks that edit buffers while that bundle is being decompressed break the download (libcurl error 61, bad content
  encoding): never edit a buffer zlib is still reading from, or keeping history of.

## 2. How ads reach the screen (TV shows and movies)

Two sources, handled differently:

1. **PRS / `intraTitlePlaylist`** (movies): the playback-resources response embeds an ad list. Items of type `Remote`
   are resolved later through `/cdp/getVideoAds`; the others are inline *(upstream)*. This project leaves that path to the
   upstream filter (`remote_strip.*`).
2. **`getVideoAds` ("regolith") response** (TV shows, and the `Remote` items above): JSON with
   `description.adDeliverySessionId` (looks like `<hex>_PBP_EXPL_<token>`), `description.adMarkerId` (for example
   `PRE_ROLL`), a `playlist` array of ad objects (each with `media.urls` pointing to `.mpd` manifests) and a
   `measurement` object. The ad-break manager turns each `playlist` element into an ad period. With an empty array it runs
   its own "empty break" path (sends empty-break tracking events and unblocks the playlist index) and the content
   continues *(upstream; confirmed again here)*.

Observed sizes: 17 to 70 KB of decompressed JSON per ad response, delivered by libcurl's content-decoding writer in
**16384-byte** pieces (the first piece is always exactly 16384 bytes), so 2 to 6 pieces. Pre-rolls, mid-rolls and
scrubs across a break each produce one such response.

How to recognise one: its first piece starts with `{"description":{"adDeliverySessionId"`. A PRS response starts with
`{"sessionization":{"sessionHandoffToken"` and contains `intraTitlePlaylist` instead.

## 3. Why `memcpy` hooks fail on big responses

Upstream hooks the GOT slots of `memcpy` / `memmove` in `libignite.so` and edits the destination buffer of a copy that
contains a complete `"playlist":[...]`. That works when the whole array fits in one piece. In a 4-day log of the author's
device, 22 breaks were seen and 16 missed: the array was cut at the end of the first piece.

What we tried to follow the rest, and what the logs showed (every experiment was negative):

* Look for the next piece at `dst == base + len` of the first buffer: 215 logged copies landed at unrelated offsets
  (`base+N`, `base-N`), never exactly `base+len`.
* Hook `realloc` / `free` of `libignite.so` and `memcpy` / `realloc` / `free` of `libc++_shared.so` to follow the buffer:
  the hooks fired, the continuation never did.
* Probe the memory right after `base+len` for 6 s after arming: it held unrelated heap objects (escaped telemetry JSON,
  then base64 tokens), not the continuation.
* Search every copy of 32 bytes or more for the tail marker `"measurement"`: none. The rest of the response never goes
  through a hookable copy.

Conclusion: the continuation is written by code inside `libignite.so` itself (zlib output buffers, inlined copies), so no
import hook can see it. The one place where every decoded byte passes, in order, is zlib's `inflate()`.

## 4. Finding a stripped, statically linked `inflate()`

1. `strings -t x libignite.so | grep -E 'incorrect (header|data) check|unknown compression method'`. The strings sit in
   `.rodata`.
2. Thumb-2 code reaches a string through `movw rX,#lo ; movt rX,#hi ; add rX,pc` or `ldr rX,[pc,#n] ; add rX,pc`.
   Sweep `.text` with Capstone (`CS_MODE_THUMB`), resolve those pairs, keep the ones that land on the strings
   (`tools/find_inflate.py`). All three strings are referenced from one function: `inflate()` (6.23.23: file offset
   `0xd331a0`, about 6.9 KB, state machine inlined; `inflate_fast` is a separate function after it).
3. Sanity check on the prologue: it tests `strm`, `strm->zalloc` (offset 0x20), `strm->zfree` (0x24) and `strm->state`
   (0x1c) and returns `Z_STREAM_ERROR` (-2) if one is null. The first instructions are
   `push {r4-r7,lr}; add r7,sp,#0xc; push.w {r8,sb,sl,fp}`: 8 position-independent bytes.
4. z_stream layout (32-bit): `next_in@0 avail_in@4 total_in@8 next_out@12 avail_out@16 total_out@20`.

## 5. Patching code in a running app (no root)

* `mprotect(page, RWX)` on the file-backed text page of a library the app loaded **is allowed** on Fire OS 8
  (Android 11) for an app process: no SELinux `execmod` denial. Do it in one step (RWX), not RW then RX, because other
  threads may be executing in that page.
* Write the 4-byte literal (our proxy address | 1 for Thumb) before the `ldr.w pc,[pc,#0]` that uses it
  (bytes `DF F8 00 F0`), call `__builtin___clear_cache`, then restore `R-X`. The trampoline lives in our own library:
  it replays the overwritten instructions, then jumps to `inflate+8`.
* The prologue bytes are verified before any write, and the build-id selects the offset, so a different build is left
  untouched.

## 6. What the logs look like (19 Oct 2026, Fire TV Stick 4K)

```
PVINFL gated ad response, first chunk n=16384
PVKILL path=tv-inflate chunks=4 blanked=57716
```

After a successful empty, the progress bar loses its ad marker and its total duration shrinks (54:39 became 41:30 on one
episode: the ad time disappears from the timeline).

## 7. Pitfalls we hit, so you do not

* `dumpsys media_session` reports the last state change, not a live position: a "frozen" position is not a freeze.
* A fast `DPAD_RIGHT` scrub shows a thumbnail and needs `DPAD_CENTER` to commit.
* A screenshot of 1920x810 means another app's ultrawide mode, not Prime (Prime renders at 1920x1080): handy to tell apps apart.
* `adb install -r` kills the app, and the first launch afterwards can die within seconds on some boots: check before
  concluding anything.
* Never send a blind key press to a shared TV: it can launch another app.
