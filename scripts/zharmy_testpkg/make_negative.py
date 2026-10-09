#!/usr/bin/env python3
"""Negative-test packages for the engine's loader (docs/ARMY_PACKAGES.md), derived from real converter output.

    cd tools
    python3 -m zharmy convert <starter data> <built starter pack> --base <starter data> <built starter pack> \
        --faction FactionIronwood --tag IRB --requires none -o irb.zharmy        (also --tag FX, --tag IW, and
                                                                                   --tag IRZ --requires zerohour)
    python3 scripts/zharmy_testpkg/make_negative.py --irb irb.zharmy --fx base_FX.zharmy --iw base_IW.zharmy \
        --req base_req.zharmy -o <dir>

Writes one package per case into <dir> and prints "<file>  <what the engine must say>". The content hash is
recomputed after an edit unless the case is about the hash. Standard library only.
"""

import argparse
import hashlib
import zlib
import json
import os
import struct
import zipfile


def load(path):
    with zipfile.ZipFile(path) as z:
        return {i.filename: z.read(i.filename) for i in z.infolist()}


def content_hash(files):
    h = hashlib.sha256()
    for name in sorted(n.lower() for n in files if n.lower() != "manifest.json"):
        data = next(d for n, d in files.items() if n.lower() == name)
        h.update(name.encode("utf-8") + b"\0" + struct.pack("<Q", len(data)) + struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF))
    return "sha256:" + h.hexdigest()


def write(path, files, manifest=None, rehash=True, raw_manifest=None):
    m = json.loads(files["manifest.json"]) if manifest is None else manifest
    if rehash:
        m["contentHash"] = content_hash(files)
    out = dict(files)
    out["manifest.json"] = raw_manifest if raw_manifest is not None else (json.dumps(m, indent=2) + "\n").encode()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in out.items():
            z.writestr(zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)), data, zipfile.ZIP_DEFLATED)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--irb", required=True)
    ap.add_argument("--fx", required=True)
    ap.add_argument("--iw", required=True)
    ap.add_argument("--req", required=True)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    cases = []

    def case(name, expect, files, **kw):
        write(os.path.join(a.out, name + ".zharmy"), files, **kw)
        cases.append((name + ".zharmy", expect))

    irb = load(a.irb)
    fx = load(a.fx)
    iw = load(a.iw)
    req = load(a.req)
    man = json.loads(irb["manifest.json"])

    # 1. definitions that exist already
    f = dict(fx)
    f["army/ini/fxlist.ini"] += b"\nFXList FX_StarterRifleShot\n  ParticleSystem\n    Name = FX_FX_StarterMuzzleFlash\n  End\nEnd\n"
    case("redefine-prefixed", "skipped: FXList 'FX_StarterRifleShot' already exists (tag FX, so the prefix is right)", f)

    f = dict(irb)
    f["army/ini/armor.ini"] += b"\nArmor IronwoodInfantryArmor\n  Armor = Default 100%\nEnd\n"
    case("redefine-base", "skipped: Armor 'IronwoodInfantryArmor' does not start with IRB_ (a package may only add definitions with its own prefix)", f)

    # 2. duplicate tag: two packages, ids differ, tag is the same (this one has the id that sorts first)
    m = dict(man)
    m["id"] = "irb.aaa-copy"
    case("dup-tag", "with irb.zharmy: irb.ironwood skipped: the tag IRB is already used by the package 'irb.aaa-copy'", dict(irb), manifest=m, rehash=False)

    # 3. shadowed base file: art/w3d/iw_hq.w3d exists in the game data (tag IW)
    f = dict(iw)
    f["art/w3d/iw_hq.w3d"] = f[sorted(n for n in f if n.endswith(".w3d"))[0]]
    case("shadow-base-file", "skipped: file 'art/w3d/iw_hq.w3d' already exists in the game data or in another package", f)

    # 4. requires mismatch (the game runs on 'starter')
    case("requires-zerohour", "skipped: needs the game data 'zerohour', this game runs on 'starter'", dict(req), rehash=False)

    # 5. corrupt zip: bytes in the middle of the central directory are overwritten
    path = os.path.join(a.out, "corrupt-zip.zharmy")
    write(path, dict(irb))
    raw = bytearray(open(path, "rb").read())
    cd = raw.rfind(b"PK\x01\x02")
    raw[cd + 4:cd + 40] = b"\xff" * 36
    open(path, "wb").write(bytes(raw))
    cases.append(("corrupt-zip.zharmy", "skipped: corrupt zip archive / checksum / unsupported ..."))
    write(os.path.join(a.out, "truncated-zip.zharmy"), dict(irb))
    raw = open(os.path.join(a.out, "truncated-zip.zharmy"), "rb").read()
    open(os.path.join(a.out, "truncated-zip.zharmy"), "wb").write(raw[:len(raw) // 2])
    cases.append(("truncated-zip.zharmy", "skipped: not a zip archive (no end record)"))
    open(os.path.join(a.out, "not-a-zip.zharmy"), "wb").write(b"this is not a zip file" * 10)
    cases.append(("not-a-zip.zharmy", "skipped: not a zip archive"))
    # one entry's data damaged but the directory intact (stored entry, crc check)
    f = dict(irb)
    path = os.path.join(a.out, "bad-crc.zharmy")
    write(path, f)
    with zipfile.ZipFile(path) as z:
        info = z.getinfo("army/ini/weapon.ini")
    raw = bytearray(open(path, "rb").read())
    # local header: 30 bytes + name + extra, then deflated data; flip a bit in the middle of the data
    off = info.header_offset + 30 + len(info.filename.encode()) + 4
    raw[off + 6] ^= 0x55
    open(path, "wb").write(bytes(raw))
    cases.append(("bad-crc.zharmy", "skipped: corrupt package: cannot decompress / checksum mismatch in 'Army/INI/Weapon.ini'"))

    # 6. bad manifests
    case("manifest-not-json", "skipped: manifest.json: ...", dict(irb), raw_manifest=b"{ this is not json", rehash=False)
    m = dict(man); del m["tag"]
    case("manifest-no-tag", "skipped: manifest.json: 'tag' is missing or not valid ...", dict(irb), manifest=m, rehash=False)
    m = dict(man); m["format"] = 2
    case("manifest-format2", "skipped: package format 2 is newer than this game understands (1)", dict(irb), manifest=m, rehash=False)
    m = dict(man); del m["requires"]
    case("manifest-no-requires", "skipped: manifest.json: 'requires' is missing ...", dict(irb), manifest=m, rehash=False)
    m = json.loads(json.dumps(man)); m["factions"][0]["side"] = "Ironwood"
    case("manifest-bad-side", "skipped: manifest.json: faction 1: side 'Ironwood' does not start with IRB_", dict(irb), manifest=m, rehash=False)

    # 7. hash mismatch
    f = dict(irb)
    f["army/ini/weapon.ini"] += b"; changed after the hash was made\n"
    case("hash-mismatch", "skipped: the content hash does not match (the package was changed or is damaged)", f, rehash=False)

    # 8. more rule breaks
    f = dict(irb)
    f["army/ini/object.ini"] = f["army/ini/object.ini"].replace(b"Side = IRB_Ironwood", b"Side = Ironwood", 1)
    case("object-other-side", "skipped: an object uses the side 'Ironwood', which is not a side of this package", f)
    f = dict(irb)
    f["data/ini/object.ini"] = b""
    case("extra-entry", "skipped: entry 'data/ini/object.ini': not allowed in a package ...", f)
    f = dict(irb)
    f["army/ini/weapon.ini"] += b"\nArmor IRB_Sneaky\n  Armor = Default 100%\nEnd\n"
    case("wrong-block-in-file", "skipped: Army/INI/weapon.ini line N: 'Armor' blocks are not allowed in this file (only Weapon)", f)
    f = dict(irb)
    f["art/textures/other.tga"] = b"x"
    case("file-without-tag", "skipped: entry 'art/textures/other.tga': file name does not start with the tag (or ZHC and the tag)", f)

    # 8b. the two contract additions: Crate.ini and ZHC<TAG> texture names (the first two must load)
    f = dict(irb)
    f["army/ini/crate.ini"] = (b"CrateData IRB_TestCrate\n  CreationChance = 0.0\n  KilledByType = STRUCTURE\n"
                               b"  CrateObject = IRB_IronwoodWorker 1.0\nEnd\n")
    case("ok-crate", "loaded: Crate.ini with a CrateData block", f)
    f = dict(irb)
    f["art/textures/zhcirb_test.tga"] = f[sorted(n for n in f if n.endswith(".tga"))[0]]
    case("ok-zhc-texture", "loaded: texture named ZHC<TAG>...", f)
    f = dict(irb)
    f["army/ini/crate.ini"] = b"CrateData SalvageCrate\n  CreationChance = 0.0\nEnd\n"
    case("crate-unprefixed", "skipped: CrateData 'SalvageCrate' does not start with IRB_", f)
    f = dict(irb)
    f["art/textures/zhcxyz_test.tga"] = f[sorted(n for n in f if n.endswith(".tga"))[0]]
    case("zhc-wrong-tag", "skipped: entry 'art/textures/zhcxyz_test.tga': file name does not start with the tag (or ZHC and the tag)", f)

    # 9. a definition the pre-checks cannot see: the INI reader fails halfway, the game must stop and name the package
    f = dict(irb)
    f["army/ini/weapon.ini"] = f["army/ini/weapon.ini"].replace(b"Weapon IRB_IronwoodRifle\n", b"Weapon IRB_IronwoodRifle\n  PrimaryDamage = not_a_number\n", 1)
    case("parse-failure", "FAILED (game stops): the army package 'irb.ironwood' could not be loaded completely ...", f)

    for name, expect in cases:
        print("%-24s %s" % (name, expect))


if __name__ == "__main__":
    main()
