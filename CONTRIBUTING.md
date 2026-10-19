# Contributing

Thanks for wanting to help. This project is small, low-level and device-specific, so the most valuable contributions are
often **reports and measurements**, not code. All of these are welcome:

| You have... | Do this |
|---|---|
| A Fire TV / Android TV and Prime Video 6.23.23 | Install it, use it for a few days, send a **device report** (issue template). Include `python tools/events.py summary`. |
| A different Prime Video build (6.23.x, 6.24.x...) | Try `tools/find_inflate.py` on its `libignite.so`; open a **new build** issue or a PR adding one line to `kBuilds[]` with your test log. |
| An ad that got through | Open a **bug report** with the `events.log` lines around it, the title and when it appeared. |
| An arm64 device | See the arm64 item in [ROADMAP.md](ROADMAP.md): the trampoline in `inflate_hook.cpp` is ARM32/Thumb-2 only. |
| C++ / reverse-engineering skills | Pick an item in [ROADMAP.md](ROADMAP.md) or an issue labelled `good first issue` / `help wanted`. |
| Docs fixes | PRs welcome, especially for the install steps on other OSes and other signing setups. |

## Ground rules

1. **No Amazon code or binaries, ever.** No APKs, no decompiled or disassembled sources, no extracted libraries, no
   keys or tokens, no account data. Facts, offsets, short byte excerpts and logs are fine. PRs that add such content are
   closed. This keeps the project legal and alive.
2. **No credentials or personal data** in issues or logs (IP addresses, account names, device serials): redact them.
3. **Be honest in the numbers.** Say which build, device and title you tested, how many ad breaks, how many were emptied.
   "It works" without that is not useful; "9/10, one missed, log attached" is gold.
4. **Respect the upstream projects** (see [NOTICE.md](NOTICE.md)): fixes that belong in the Morphe patches should be offered there too.
5. Be kind. See the [code of conduct](CODE_OF_CONDUCT.md).

## Code contributions

* Native code lives in `src/native` (C++17, ARM32 target). Keep functions in the hot paths (`proxy_inflate`) allocation-free
  and non-blocking: they run on the app's decoding threads.
* Never edit a buffer while zlib is still reading from it or keeping history of it (see `docs/HOW_IT_WORKS.md`).
* Any new patch site must verify bytes before writing, log a `HOOK-OFF` event with the reason when it declines, and leave
  unknown builds untouched.
* Add or extend a unit test when you touch the JSON scanning logic (`src/native/test_*.cpp`, built by CI with `g++`).
* Build: `ANDROID_NDK_HOME=... tools/build.sh`. Run the tests on a device with `tools/test.sh`, or on a PC:
  `g++ -std=c++17 src/native/test_rego_stream.cpp -o t && ./t`.
* Commit messages: a short imperative subject; explain *why* in the body when it is not obvious.
* Open a draft PR early if the change is big. CI must be green.

## Testing a change on a real TV

Use a TV nobody is watching (installing restarts Prime). After `adb install -r`, play a title with ads, then
`python tools/events.py pull && python tools/events.py summary`. A good run shows as many `AD-GATED` as `AD-KILLED`
and no `AD-MISSED`, `AD-UNGATED`, `CRASH` or `HOOK-OFF`. Paste the summary in your PR.

## Reporting security issues

If you find a problem that could harm users (for example a crash loop or a way to abuse the event log), open a private
security advisory on GitHub instead of a public issue.
