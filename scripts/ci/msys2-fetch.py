#!/usr/bin/env python3
"""Unpack MSYS2 packages (prebuilt Windows libraries) on Linux.

Used by the Windows cross-build: the libraries the app links against come from
MSYS2's CLANG64 repository, the same binaries a native MSYS2 build would use.
This is a small stand-in for pacman: it reads the repository index, follows
dependencies, checks every download against the index's SHA-256 and unpacks
the packages below a prefix.

    msys2-fetch.py --prefix /opt/win/msys2 [--runtime-only NAMES] [--skip NAMES] PACKAGE...

Package names are given without the "mingw-w64-clang-x86_64-" prefix.

--skip          packages that are neither downloaded nor followed (the compiler
                and its runtime come from llvm-mingw, not from MSYS2)
--runtime-only  packages of which only the DLLs are kept, in <prefix>/runtime-bin,
                so that their headers and import libraries can't shadow the
                toolchain's own

Needs python3 and the zstd and tar commands.
"""

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request

REPO = "clang64"
PKG_PREFIX = "mingw-w64-clang-x86_64-"
MIRRORS = [
    "https://mirror.msys2.org/mingw/" + REPO,
    "https://repo.msys2.org/mingw/" + REPO,
]


def fetch(name, dest, sha256=None):
    """Download repo file `name` to `dest`, trying each mirror a few times."""
    last_error = None
    for attempt in range(4):
        for mirror in MIRRORS:
            url = mirror + "/" + name
            try:
                tmp = dest + ".part"
                req = urllib.request.Request(url, headers={"User-Agent": "msys2-fetch"})
                with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                    shutil.copyfileobj(r, f)
                if sha256:
                    h = hashlib.sha256()
                    with open(tmp, "rb") as f:
                        for chunk in iter(lambda: f.read(1 << 20), b""):
                            h.update(chunk)
                    if h.hexdigest() != sha256:
                        raise RuntimeError("checksum mismatch for " + name)
                os.replace(tmp, dest)
                return
            except Exception as e:  # noqa: BLE001 - any failure means "try the next mirror"
                last_error = e
        time.sleep(2 + attempt * 3)
    raise SystemExit("error: cannot download %s: %s" % (name, last_error))


def read_index(cache_dir):
    db_path = os.path.join(cache_dir, REPO + ".db")
    fetch(REPO + ".db", db_path)
    raw = subprocess.run(["zstd", "-dc", db_path], check=True, stdout=subprocess.PIPE).stdout
    packages = {}
    provides = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as tf:
        for member in tf.getmembers():
            if not member.name.endswith("/desc"):
                continue
            fields = {}
            key = None
            for line in tf.extractfile(member).read().decode("utf-8").splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line.strip() and key:
                    fields[key].append(line.strip())
            name = fields["NAME"][0]
            packages[name] = fields
            for p in fields.get("PROVIDES", []):
                provides.setdefault(re.split(r"[<>=]", p)[0], name)
    return packages, provides


def strip_constraint(dep):
    return re.split(r"[<>=]", dep)[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--skip", default="")
    ap.add_argument("--runtime-only", default="")
    ap.add_argument("--cache", default=None, help="directory for downloads (default: <prefix>/.cache)")
    ap.add_argument("packages", nargs="+")
    args = ap.parse_args()

    prefix = os.path.abspath(args.prefix)
    cache = args.cache or os.path.join(prefix, ".cache")
    os.makedirs(cache, exist_ok=True)
    skip = {PKG_PREFIX + n for n in args.skip.split(",") if n}
    runtime_only = {PKG_PREFIX + n for n in args.runtime_only.split(",") if n}

    packages, provides = read_index(cache)

    def resolve(name):
        if name in packages:
            return name
        if name in provides:
            return provides[name]
        raise SystemExit("error: MSYS2 has no package called " + name)

    wanted = []
    seen = set()
    queue = [resolve(PKG_PREFIX + n) for n in args.packages] + sorted(resolve(n) for n in runtime_only)
    while queue:
        name = queue.pop(0)
        if name in seen or name in skip:
            continue
        seen.add(name)
        wanted.append(name)
        if name in runtime_only:
            continue
        for dep in packages[name].get("DEPENDS", []):
            dep = resolve(strip_constraint(dep))
            if dep not in seen and dep not in skip:
                queue.append(dep)

    installed = {}
    root = os.path.join(prefix, REPO)
    runtime_bin = os.path.join(prefix, "runtime-bin")
    os.makedirs(root, exist_ok=True)
    os.makedirs(runtime_bin, exist_ok=True)

    for name in sorted(wanted):
        fields = packages[name]
        filename = fields["FILENAME"][0]
        version = fields["VERSION"][0]
        archive = os.path.join(cache, filename)
        if not os.path.exists(archive):
            fetch(filename, archive, fields["SHA256SUM"][0])
        short = name[len(PKG_PREFIX):]
        if name in runtime_only:
            tmp = os.path.join(cache, "unpack-" + short)
            shutil.rmtree(tmp, ignore_errors=True)
            os.makedirs(tmp)
            subprocess.run(["tar", "--zstd", "-xf", archive, "-C", tmp, REPO + "/bin"], check=True)
            for f in os.listdir(os.path.join(tmp, REPO, "bin")):
                if f.lower().endswith(".dll"):
                    shutil.copy2(os.path.join(tmp, REPO, "bin", f), os.path.join(runtime_bin, f))
            shutil.rmtree(tmp)
            print("  %-28s %s (runtime DLLs only)" % (short, version))
        else:
            subprocess.run(
                ["tar", "--zstd", "-xf", archive, "-C", prefix, "--exclude=.*", REPO],
                check=True,
            )
            print("  %-28s %s" % (short, version))
        installed[short] = version
        os.remove(archive)

    # MSYS2 writes its own install location (/clang64) into pkg-config and CMake
    # files; point those at where the files really are.
    pattern = re.compile(r"(?<![\w/.\-])/" + REPO + r"(?=[/\s\"';]|$)", re.M)
    for sub in ("lib/pkgconfig", "share/pkgconfig", "lib/cmake", "share/cmake"):
        top = os.path.join(root, sub)
        for dirpath, _dirs, files in os.walk(top):
            for f in files:
                if not f.endswith((".pc", ".cmake")):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
                    text = fh.read()
                fixed = pattern.sub(root, text)
                if fixed != text:
                    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
                        fh.write(fixed)

    with open(os.path.join(prefix, "packages.json"), "w") as fh:
        json.dump(installed, fh, indent=1, sort_keys=True)
    print("%d MSYS2 packages unpacked into %s" % (len(installed), root))


if __name__ == "__main__":
    sys.exit(main())
