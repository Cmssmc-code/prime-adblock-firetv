# Credits

This project builds on **[ajstrick81/morphe-androidtv-patches](https://github.com/ajstrick81/morphe-androidtv-patches)**
(GPL-3.0, v1.42.2), which provides the Morphe patches that load a native library (`libpvhook.so`) into
the Prime Video Android TV app, and the first generation of that library: the GOT import hook
(`got_hook.cpp`), the `memcpy`-level response filters (`hooks.cpp`, `remote_strip.*`, `rego_filter.h`) and
the research that located where ad responses can be edited safely. Those files are derived from that
project and keep its GPL-3.0 license.

Added here: the inline hook on zlib's `inflate()` (`inflate_hook.*`), the persistent event log and crash
recorder (`evlog.*`), the response follower experiments (`rego_stream.h`, disabled by default), the porting
and analysis tools, and the documentation.

"Prime Video", "Fire TV" and "Amazon" are trademarks of Amazon.com, Inc. This project is not affiliated
with or endorsed by Amazon. It contains no Amazon code or binaries.
