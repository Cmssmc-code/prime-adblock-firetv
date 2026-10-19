# Supporting another Prime Video version

The hook is tied to one `libignite.so` build because `inflate()` is stripped. To add a build:

1. Get the library: install the Morphe-patched Prime Video, `adb shell pm path com.amazon.amazonvideo.livingroom.mod`,
   `adb pull` the APK (or the `config.armeabi_v7a` split) and extract `lib/armeabi-v7a/libignite.so`.
2. `pip install capstone pyelftools` then `python tools/find_inflate.py libignite.so`
   (a few minutes: it sweeps the 7 MB of Thumb code).
3. Paste the printed line into `kBuilds[]` in `src/native/inflate_hook.cpp`, rebuild (`tools/build.sh`), swap the
   library (`tools/swap_lib.py`), install, play something with ads, then
   `python tools/events.py pull && python tools/events.py summary`.

If the script says the prologue is not position independent, the first 8 bytes contain pc-relative instructions
and the trampoline must replay a different number of bytes. If no function references all three zlib strings,
the app no longer embeds zlib that way (or uses ARM mode code); look for `inflate` through Ghidra using the
same strings (`incorrect header check`, `unknown compression method`, `incorrect data check`).

Other ABIs (arm64) need a different hook (A64 trampoline); not implemented.
