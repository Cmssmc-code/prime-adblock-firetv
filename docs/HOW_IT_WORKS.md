# How it works

## The ad pipeline (Prime Video for Android TV, "SGAI")

Ad breaks are not baked into the video. Shortly before a break (and once at start for the pre-roll) the
player calls `getVideoAds`; the JSON answer looks like

```json
{"description":{"adDeliverySessionId":"…_PBP_EXPL_…","adMarkerId":"PRE_ROLL"},
 "playlist":[{ …ad… },{ …ad… }], "measurement":{ … }}
```

and the app builds one ad period per element of `playlist`. If `playlist` is empty the app takes its own
clean "empty break" path: no period is created and playback continues. So the goal is simply:
**make `playlist` an empty array before the app parses it**, keeping the JSON valid and the length unchanged.

## Why the older hooks missed 70 % of the breaks

`libignite.so` (the native player core) statically links **libcurl and zlib**. Large responses are gunzipped by
libcurl in several `inflate()` calls and reach the app in several chunks. A hook on `memcpy` (GOT import slot)
sees the first chunk; when the `playlist` array is cut off in it, the rest arrives later through buffers that
no import hook can see (it is written by zlib/NEON code inside the library). In the author's log, 16 of 22 breaks
were missed this way. Tracking the buffer (realloc/free hooks, memory probes) never found the continuation.

## The fix: hook `inflate()` itself

Every decoded byte of every response passes through zlib's `inflate()`, in order. The hook (`inflate_hook.cpp`):

1. Overwrites the first 8 bytes of `inflate()` (3 position-independent Thumb instructions) with
   `ldr.w pc,[pc,#0]` + the address of our proxy. A small assembly trampoline replays the 3 instructions and
   jumps to `inflate()+8`.
2. After the real `inflate()` returns, looks at the bytes it just produced (`next_out` … new `next_out`).
3. A stream whose first chunk starts with `{` and contains `"adDeliverySessionId"` (and not `intraTitlePlaylist`,
   the in-manifest ad list handled by another path) is an ad decision. For that z_stream it finds
   `"playlist":[` — also across chunk boundaries — then blanks everything up to the matching `]` with spaces
   (string-aware bracket matching), over as many chunks as it takes (up to 6 seen).
4. Result: `"playlist":[        ]` — valid JSON, same length.

Editing the output after `inflate()` returned is safe: zlib has already updated its checksum and copied the history
window. (Editing buffers *while* decompression is running is what corrupted streams in earlier attempts: CURLE 61.)

The patch is applied only to a build whose GNU build-id is in the table in `inflate_hook.cpp` and whose prologue
bytes match; otherwise the hook does nothing and logs `HOOK-OFF`.

## Observability

`evlog.cpp` writes `/sdcard/Android/data/<pkg>/files/pvhook_events.log` (no permission needed, readable over adb,
rotated at 1 MB): `START`, `HOOK-ON/OFF`, `AD-GATED` (an ad response was recognised), `AD-KILLED` (array emptied),
`AD-MISSED`, `AD-UNGATED` (a playlist-like response we do not recognise), and `CRASH` (a signal whose pc/lr is inside
libignite or this library; the signal is then passed on unchanged). `tools/events.py` summarises it.

## What it does not do

* It does not touch the player, the DRM, accounts or the network.
* It does not remove ads baked into the stream itself (live TV, some sports) or the "Regarder sans publicité" upsell.
* It cannot work on a different `libignite.so` build until its offset is added (see PORTING.md).
