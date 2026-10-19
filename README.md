# prime-adblock-firetv

Remove the ads from **Prime Video on Fire TV** by editing the ad-decision response inside the app, at the
exact point where it is decompressed. No DNS tricks, no proxy, no rooted device.

> **Status (19 Oct 2026):** works on Prime Video **6.23.23** (Morphe-patched, `armeabi-v7a`) on a Fire TV Stick 4K
> (AFTKM, Fire OS 8 / Android 11). In an afternoon of testing (Carrie, Twilight, The Hunger Games; pre-rolls,
> mid-rolls, fast scrubbing across breaks) **10 of 10 ad responses were emptied**, no ad was shown, no crash.
> That is a small sample on one device and one app build: please read [Limitations](#limitations).
>
> **Which code is which.** Tag `v0.1-verified` (branch `verified-v0.1`) is the exact revision that produced those results,
> and `prebuilt/libpvhook-v0.1-verified-armeabi-v7a.so` is built from it. `main` adds a persistent event log, a crash
> recorder and a build-id table on top: it compiles but has **not yet been run on a device**; this README will be updated
> when it has.

## Thank you

None of this exists without the people who did the hard part first. **Huge thanks to:**

* **[ajstrick81](https://github.com/ajstrick81)** and the **[morphe-androidtv-patches](https://github.com/ajstrick81/morphe-androidtv-patches)**
  project: the Prime Video patch, the in-app native loader (`libpvhook.so`), the GOT import hook, the response filters and,
  above all, the research that found where an ad response can be edited safely. This repository is built directly on that
  work (and on its public discussion of the mid-roll problem, upstream issue #120), and keeps its GPL-3.0 license.
* **The [Morphe team](https://github.com/MorpheApp/morphe-patches)** for Morphe Manager and the patching framework that makes
  it possible to modify these apps on a TV at all.
* The authors of the tools used for the analysis: the **Android NDK / LLVM**, **Capstone**, **pyelftools** and **Ghidra**.
* **APKMirror**, which hosts the official APKs everyone can patch themselves.

If this is useful to you, please star and support the upstream projects first. Fixes that make sense there should go there too.

[Version française en bas / French summary below](#résumé-français)

## Why there is no APK in this repository

Prime Video is Amazon's copyrighted software. A patched APK (or decompiled sources) would redistribute that code, which
we will not do and which would get this repository taken down. Everything here is built **from your own copy** of the app:
the official APK from APKMirror (the same download links as the upstream patches), patched on your machine. What we
publish is ours: the native library source and a prebuilt binary, the tools, and a complete explanation of the findings
([docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md)) so that you can reproduce, check and extend all of it.

## What you get

* The ad break simply never appears: playback continues, the progress bar loses its ad markers.
* A tiny native library (`libpvhook.so`, ~100 KB) that replaces the one shipped by the Morphe patches.
* A persistent on-device event log and tools to check, days later, that every break was handled.

## How it works (short)

Prime Video asks a `getVideoAds` endpoint for the ads of each break; the app builds one ad period per element of the
JSON `playlist` array. libcurl and zlib are statically linked in `libignite.so`; big answers are gunzipped over several
`inflate()` calls, and earlier hooks (on `memcpy`) only ever saw the first chunk, so ~70 % of the breaks got through.
This project patches `inflate()` itself and blanks `"playlist":[…]` in the bytes it just produced, chunk after chunk, so
the app receives a valid, same-length, empty playlist and takes its own "no ads" path.
Details: [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

## Requirements

* A Fire TV (or Android TV) running the **Morphe-patched** Prime Video: install it with Morphe Manager and the patches from
  [ajstrick81/morphe-androidtv-patches](https://github.com/ajstrick81/morphe-androidtv-patches). Package
  `com.amazon.amazonvideo.livingroom.mod`, version **6.23.23** (other versions: see [docs/PORTING.md](docs/PORTING.md)).
  **Do not update Prime Video**, and do not re-patch with Morphe afterwards (that restores the stock library).
* The signing key Morphe Manager used (Settings → System → Signing key → Export), so the update installs over the
  existing app and keeps your login. Convert a BKS keystore to PKCS12 once with `keytool -importkeystore`.
* To build: Android NDK r25+ and the Android SDK `cmake`, `ninja` and `build-tools` packages. Or use the prebuilt library in `prebuilt/`.
* `adb` access to the TV (Settings → My Fire TV → Developer options → ADB debugging).

## Install (from scratch, with your own APK)

**Step 0.** Download **Prime Video (Android TV) 6.23.23+v15.5.0.70-armv7a** (`.apkm`) from APKMirror (the link is in the
upstream README), patch it with Morphe Manager and the Prime Video patch with **"Optimize for device architecture" OFF**,
install the result on the TV and check that it plays. Then replace its native library with ours:

```bash
# 1. get the APK that is installed now
adb shell pm path com.amazon.amazonvideo.livingroom.mod        # -> .../base.apk
adb pull <that path> base.apk

# 2. build the library (or take prebuilt/libpvhook-v0.1-verified-armeabi-v7a.so)
ANDROID_NDK_HOME=/path/to/ndk tools/build.sh                   # -> build/libpvhook.so

# 3. put it in the APK, align, sign with the Morphe key
export KS_PASS='<your keystore password>'
python tools/swap_lib.py --apk base.apk --lib build/libpvhook.so --out prime-adblock.apk \
       --keystore morphe.p12 --alias morphe --ks-pass env:KS_PASS

# 4. install over the existing app (nobody should be watching: Prime restarts)
adb install -r prime-adblock.apk
```

### Signing keys, in practice

* **Keep your login (recommended):** Android only updates an app in place if the new APK is signed with the same key as
  the installed one. Export Morphe Manager's key (Settings → System → Signing key → Export, a `.keystore` in BKS format) and
  convert it once to PKCS12, which `apksigner` can read:
  `keytool -importkeystore -srckeystore morphe.keystore -srcstoretype BKS -destkeystore morphe.p12 -deststoretype PKCS12 -providerpath bcprov.jar -providerclass org.bouncycastle.jce.provider.BouncyCastleProvider`
  (any BouncyCastle `bcprov` jar works; list the alias with `keytool -list`).
* **No key to hand:** sign with a new key (`keytool -genkeypair -keystore my.p12 -storetype PKCS12 -alias me -keyalg RSA -keysize 2048 -validity 3650`),
  then `adb uninstall com.amazon.amazonvideo.livingroom.mod` before installing; you will have to sign in to Prime again.

### What has been tested end to end

Verified: building from source, `tools/swap_lib.py` (library replaced, aligned, signature valid), `tools/find_inflate.py`
(finds the 6.23.23 offset by itself), and the hook on a real Fire TV (tag `v0.1-verified`). **Not yet verified by a third
party:** the whole chain on a fresh machine, and the signing/conversion notes above on other setups. Issues and PRs with your
results are exactly what this project needs.

## Check that it works

```bash
python tools/events.py pull        # copies /sdcard/Android/data/<pkg>/files/pvhook_events.log
python tools/events.py summary
```

A healthy log has as many `AD-GATED` as `AD-KILLED`. `AD-MISSED`, `AD-UNGATED`, `CRASH` or `HOOK-OFF` (with a reason such
as `unknown-build`) are what to report. Live `logcat | grep PVNativeHook` shows the same events in real time.

## Limitations

* **One build.** `inflate()` is stripped, so its offset is keyed on the `libignite.so` GNU build-id. Another Prime version
  needs a new table entry (`tools/find_inflate.py` finds it automatically in the cases we know) and a test.
* **32-bit ARM only.** No arm64 trampoline yet.
* Tested on **one** device. Playback of titles with unusual ad structures (live, sports, "Channels") is untested.
* Ads baked into the video stream, and the paid "watch without ads" button, are out of scope.
* It requires patching an app you are entitled to use. Check your local law and Amazon's terms: you are responsible for
  what you do with it. This is a research/interoperability project, provided as is, with no warranty.
  It ships no Amazon code or binary.

## Layout

```
src/native/   C++ sources of libpvhook.so (inflate_hook.cpp is the core; evlog.cpp the event log)
tools/        build.sh, swap_lib.py, events.py, find_inflate.py, test.sh
docs/         HOW_IT_WORKS.md, PORTING.md, RESULTS.md
prebuilt/     libpvhook-v0.1-verified-armeabi-v7a.so (built from tag v0.1-verified), SHA256SUMS
```

## Contributing

Help is very welcome, and not only code: **device reports, test logs and new Prime builds** are the most valuable things
right now. Start with [CONTRIBUTING.md](CONTRIBUTING.md) and the [ROADMAP](ROADMAP.md), or pick an issue labelled
`good first issue` / `help wanted`. Questions and ideas go in [Discussions](https://github.com/Cmssmc-code/prime-adblock-firetv/discussions).
The one hard rule: no Amazon code or binaries in the repository.

## License and credits

GPL-3.0 (inherited from the Morphe patches this builds on). See [NOTICE.md](NOTICE.md). Contributions welcome,
especially: new Prime builds (PRs adding a `kBuilds[]` line with a test log), arm64, and reports from other devices.

---

## Résumé français

Supprime les pubs de **Prime Video sur Fire TV** en vidant la liste des pubs au moment où la réponse est décompressée
dans l'appli (hook inline sur `inflate()` de zlib, statique dans `libignite.so`). Pas de DNS, pas de proxy, pas de root.
Testé sur Prime 6.23.23 patché Morphe, Fire TV Stick 4K : 10 réponses de pub sur 10 vidées, aucune pub, aucun plantage.
Installation : voir les commandes ci-dessus. **Ne pas mettre Prime à jour.** Le journal `pvhook_events.log` (sur la TV) et
`tools/events.py summary` permettent de vérifier plus tard que toutes les coupures ont été traitées.
