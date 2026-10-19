#!/usr/bin/env python3
"""Locate zlib's inflate() inside a stripped libignite.so (ARM32, Thumb-2) and print the table
entry for src/native/inflate_hook.cpp.

    pip install capstone pyelftools
    python tools/find_inflate.py path/to/libignite.so

How it works: inflate() is the only function that mentions "incorrect header check",
"unknown compression method" and "incorrect data check". Thumb-2 code reaches a string with
    movw rX,#lo ; movt rX,#hi ; add rX,pc        or        ldr rX,[pc,#n] ; add rX,pc
so we sweep .text, resolve those pc-relative pairs, keep the ones landing on the three strings
and take the nearest preceding `push {..,lr}` as the function start.
It then checks that the first 8 bytes are position independent (they are copied into a
trampoline); if not, the hook needs a different patch size and this script says so.
"""
import re, struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
from elftools.elf.elffile import ELFFile

STRINGS = [b"incorrect header check", b"unknown compression method", b"incorrect data check"]
SAFE = ("push", "add", "mov", "sub", "stmdb", "nop")      # no pc-relative / branch instructions

def main(path):
    d = open(path, "rb").read()
    elf = ELFFile(open(path, "rb"))
    text = elf.get_section_by_name(".text")
    t0, ts = text["sh_addr"], text["sh_size"]
    targets = {}
    for s in STRINGS:
        for m in re.finditer(re.escape(s) + b"\x00", d):
            targets[m.start()] = s.decode()
    if len(set(targets.values())) < 3:
        sys.exit("zlib error strings not found: this library does not embed zlib's inflate")
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB); md.skipdata = True
    pushes, hits, prev = [], [], []
    for ins in md.disasm(d[t0:t0 + ts], t0):
        m, ops = ins.mnemonic, ins.op_str
        if m.startswith("push") and "lr" in ops:
            pushes.append(ins.address)
        prev.append(ins); prev = prev[-6:]
        if m.startswith("add") and "pc" in ops:
            r = ops.split(",")[0].strip(); lo = hi = None
            for p in reversed(prev[:-1]):
                if p.mnemonic.startswith("movw") and p.op_str.startswith(r + ","): lo = int(p.op_str.split("#")[1], 0)
                if p.mnemonic.startswith("movt") and p.op_str.startswith(r + ","): hi = int(p.op_str.split("#")[1], 0)
                if p.mnemonic.startswith("ldr") and p.op_str.startswith(r + ", [pc"):
                    imm = int(p.op_str.split("#")[1].rstrip("]"), 0) if "#" in p.op_str else 0
                    a = ((p.address + 4) & ~3) + imm
                    if a + 4 <= len(d): lo = struct.unpack("<I", d[a:a + 4])[0]; hi = 0
            if lo is not None and hi is not None:
                tgt = (((hi << 16) | lo) + ins.address + 4) & 0xFFFFFFFF
                if tgt in targets: hits.append((ins.address, targets[tgt]))
    if not hits:
        sys.exit("no code references to the strings were found (ARM mode code? different compiler?)")
    import bisect
    funcs = {}
    for addr, name in hits:
        i = bisect.bisect_right(pushes, addr) - 1
        funcs.setdefault(pushes[i], set()).add(name)
    best = [f for f, names in funcs.items() if len(names) == 3]
    if not best:
        print("no single function references all three strings; candidates:")
        for f, n in funcs.items(): print(f"  0x{f:x}: {sorted(n)}")
        sys.exit(1)
    fn = best[0]
    code = d[fn:fn + 8]
    ok = all(i.mnemonic.split(".")[0] in SAFE for i in md.disasm(code, fn))
    bid = ""
    for sec in elf.iter_sections():
        if sec.name == ".note.gnu.build-id":
            for n in sec.iter_notes(): bid = n["n_desc"]
    print(f"inflate() at file offset 0x{fn:x}  (first 8 bytes: {code.hex(' ')})")
    for i in md.disasm(code, fn): print(f"  {i.address:x}: {i.mnemonic} {i.op_str}")
    print("position independent prologue:", "yes" if ok else "NO - the 8-byte patch would break, extend the trampoline")
    print("build-id:", bid or "(none: key the table on something else)")
    print("\nAdd to kBuilds[] in src/native/inflate_hook.cpp:")
    print(f'    {{"{bid}", 0x{fn:x}, {{{", ".join("0x%02x" % b for b in code)}}}}},')

if __name__ == "__main__":
    main(sys.argv[1]) if len(sys.argv) == 2 else sys.exit(__doc__)
