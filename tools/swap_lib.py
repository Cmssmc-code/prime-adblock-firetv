#!/usr/bin/env python3
"""Replace lib/armeabi-v7a/libpvhook.so inside a Morphe-patched Prime Video APK, zipalign and re-sign it.

    python tools/swap_lib.py --apk base.apk --lib build/libpvhook.so --out prime-adblock.apk \
        --keystore my.p12 --alias morphe --ks-pass env:KS_PASS [--build-tools <dir>]

Use the SAME signing key as the APK you have installed (Morphe Manager can export it:
Settings > System > Signing key), otherwise Android refuses the in-place update and you
would have to uninstall first (and lose your login). Nothing is installed by this script.
"""
import argparse, os, shutil, subprocess, sys, tempfile, zipfile, glob

def find_build_tools(explicit):
    if explicit:
        return explicit
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or \
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk")
    cands = sorted(glob.glob(os.path.join(sdk, "build-tools", "*")))
    if not cands:
        sys.exit("Android build-tools not found: pass --build-tools")
    return cands[-1]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True); ap.add_argument("--lib", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--keystore", required=True)
    ap.add_argument("--alias", required=True)
    ap.add_argument("--ks-pass", required=True, help="pass:<pw> | env:<VAR> | file:<path> (apksigner syntax)")
    ap.add_argument("--ks-type", default="PKCS12"); ap.add_argument("--build-tools")
    a = ap.parse_args()
    bt = find_build_tools(a.build_tools)
    exe = ".exe" if os.name == "nt" else ""
    bat = ".bat" if os.name == "nt" else ""
    tmp = tempfile.mkdtemp(prefix="pvswap_")
    try:
        unsigned = os.path.join(tmp, "unsigned.apk")
        swapped = False
        with zipfile.ZipFile(a.apk) as zi, zipfile.ZipFile(unsigned, "w") as zo:
            for it in zi.infolist():
                n = it.filename
                if n.startswith("META-INF/") and n.rsplit(".", 1)[-1] in ("SF", "RSA", "EC", "DSA", "MF"):
                    continue                                   # old signature goes away
                if n == "lib/armeabi-v7a/libpvhook.so":
                    data = open(a.lib, "rb").read(); swapped = True
                else:
                    data = zi.read(it)
                zo.writestr(it, data, compress_type=it.compress_type)
        if not swapped:
            sys.exit("lib/armeabi-v7a/libpvhook.so not found in the APK: is it a Morphe-patched Prime Video?")
        aligned = os.path.join(tmp, "aligned.apk")
        subprocess.check_call([os.path.join(bt, "zipalign" + exe), "-f", "-p", "4", unsigned, aligned])
        subprocess.check_call([os.path.join(bt, "apksigner" + bat), "sign", "--ks", a.keystore,
                               "--ks-type", a.ks_type, "--ks-key-alias", a.alias, "--ks-pass", a.ks_pass,
                               "--out", a.out, aligned])
        subprocess.check_call([os.path.join(bt, "apksigner" + bat), "verify", "--print-certs", a.out])
        print("OK ->", a.out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
