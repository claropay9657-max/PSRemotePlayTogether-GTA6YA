#!/usr/bin/env python3
"""Collect everything the Windows build needs to run into one folder.

Does on Linux what windeployqt does on Windows (it only exists as a Windows
program): copies the Qt plugins and QML modules the app uses, then follows the
import tables of every copied binary and adds the DLLs they name.

    deploy-windows.py --root /opt/psrpt-win --exe build/gui/chiaki.exe \
        --exe-name PSRemotePlayTogether.exe --search-dir build/third-party/cpp-steam-tools \
        --qml-dir gui/src/qml --out dist/PSRemotePlayTogether
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

# Qt plugins to ship, by plugin directory. Only what the app can use.
QT_PLUGINS = {
    "platforms": ["qwindows.dll"],
    "styles": ["qmodernwindowsstyle.dll"],
    "imageformats": ["qsvg.dll", "qjpeg.dll", "qgif.dll", "qico.dll"],
    "iconengines": ["qsvgicon.dll"],
    "tls": ["qschannelbackend.dll", "qcertonlybackend.dll"],
    "networkinformation": ["qnetworklistmanager.dll"],
    "generic": ["qtuiotouchplugin.dll"],
    # Camera and microphone through Windows' own media framework. Qt's FFmpeg
    # media plugin is left out on purpose: it would add a second, full FFmpeg.
    "multimedia": ["windowsmediaplugin.dll"],
}

# Supplied by Windows or the graphics driver; never bundled
SKIP_DLLS = {"vulkan-1.dll", "opengl32.dll"}

# Not finding these next to the app is expected: they are part of Windows
SYSTEM_DLL = re.compile(
    r"^(api-ms-|ext-ms-)|^("
    r"kernel32|kernelbase|ntdll|user32|gdi32|gdi32full|advapi32|shell32|ole32|oleaut32|comdlg32|comctl32|"
    r"ws2_32|wsock32|mswsock|iphlpapi|bcrypt|bcryptprimitives|ncrypt|crypt32|secur32|sspicli|wldap32|"
    r"winmm|version|imm32|shlwapi|setupapi|cfgmgr32|hid|dwmapi|uxtheme|netapi32|userenv|wtsapi32|"
    r"dnsapi|rpcrt4|psapi|powrprof|shcore|winhttp|wininet|urlmon|normaliz|msvcrt|ucrtbase|authz|"
    r"d3d9|d3d11|d3d12|dxgi|dxva2|dcomp|dwrite|d2d1|d3dcompiler_47|mf|mfplat|mfreadwrite|mfuuid|evr|"
    r"propsys|avrt|ksuser|mmdevapi|winspool\.drv|mpr|dbghelp|dbgeng|oleacc|uiautomationcore|"
    r"wintrust|msimg32|opengl32|glu32|netprofm|wlanapi|wbemuuid|comsuppw|msvcp_win|win32u|"
    r"dinput8|xinput1_4|xinput9_1_0|dsound|winusb|mfcore|mfsensorgroup|ole32|quartz|strmiids|"
    r"credui|virtdisk|winsta|wevtapi|pdh|cabinet|msi|imagehlp|sechost|bluetoothapis|magnification"
    r")(\.dll)?$",
    re.I,
)


def is_pe(path):
    return path.lower().endswith((".dll", ".exe"))


def copy_file(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def llvm_version(text):
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", text or "")
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("PSRPT_WIN_ROOT", "/opt/psrpt-win"))
    ap.add_argument("--exe", required=True, help="the built app")
    ap.add_argument("--exe-name", default=None, help="file name to give the app in the package")
    ap.add_argument("--extra", action="append", default=[], help="further binaries to put next to the app")
    ap.add_argument("--search-dir", action="append", default=[], help="further directories with DLLs the app was built with")
    ap.add_argument("--qml-dir", required=True, help="the app's QML sources, to find the modules they import")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    out = os.path.abspath(args.out)
    qt_target = os.path.realpath(os.path.join(root, "qt/target"))
    qt_host = os.path.realpath(os.path.join(root, "qt/host"))
    objdump = os.path.join(root, "llvm-mingw/bin/llvm-objdump")
    toolchain_bin = os.path.join(root, "llvm-mingw/x86_64-w64-mingw32/bin")
    msys_bin = os.path.join(root, "msys2/clang64/bin")
    msys_runtime = os.path.join(root, "msys2/runtime-bin")

    # libc++/libunwind/winpthread exist twice: the toolchain's and MSYS2's (which
    # MSYS2's C++ libraries were built against). Only one copy can be loaded, and
    # the newer one works for both, so take whichever is newer.
    toolchain_llvm = llvm_version(
        subprocess.run([os.path.join(root, "llvm-mingw/bin/clang"), "--version"], capture_output=True, text=True).stdout
    )
    msys_llvm = (0, 0, 0)
    try:
        with open(os.path.join(root, "msys2/packages.json")) as f:
            msys_llvm = llvm_version(json.load(f).get("libc++"))
    except OSError:
        pass
    if msys_llvm > toolchain_llvm and os.path.isdir(msys_runtime):
        runtime_dirs = [msys_runtime, toolchain_bin]
    else:
        runtime_dirs = [toolchain_bin, msys_runtime]
    print("C++ runtime: toolchain LLVM %s, MSYS2 LLVM %s -> %s" % (
        ".".join(map(str, toolchain_llvm)), ".".join(map(str, msys_llvm)), runtime_dirs[0]))

    search_dirs = (
        [os.path.abspath(d) for d in args.search_dir]
        + [os.path.join(qt_target, "bin"), os.path.join(root, "ffmpeg/bin")]
        + runtime_dirs
        + [msys_bin]
    )

    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    exe_name = args.exe_name or os.path.basename(args.exe)
    copy_file(args.exe, os.path.join(out, exe_name))
    for extra in args.extra:
        copy_file(extra, os.path.join(out, os.path.basename(extra)))

    # Qt plugins
    for category, names in QT_PLUGINS.items():
        for name in names:
            src = os.path.join(qt_target, "plugins", category, name)
            if os.path.exists(src):
                copy_file(src, os.path.join(out, category, name))
            elif category != "multimedia":
                print("note: Qt plugin %s/%s is not part of this Qt" % (category, name))

    # QML modules
    scanner = os.path.join(qt_host, "libexec/qmlimportscanner")
    qml_root = os.path.join(qt_target, "qml")
    result = subprocess.run(
        [scanner, "-rootPath", args.qml_dir, "-importPath", qml_root],
        check=True, capture_output=True, text=True,
    )
    modules = json.loads(result.stdout)

    # What the app's own QML files import. Qt's modules import further ones
    # (styles for other systems, for example) that this Qt need not have.
    direct_imports = set()
    for dirpath, _dirs, files in os.walk(args.qml_dir):
        for f in files:
            if f.endswith(".qml"):
                with open(os.path.join(dirpath, f), encoding="utf-8") as fh:
                    direct_imports.update(re.findall(r"^\s*import\s+([A-Za-z][\w.]*)", fh.read(), re.M))
    # Registered from C++, or (WebEngine) not offered by Qt for this compiler
    direct_imports = {m for m in direct_imports if not m.startswith(("org.streetpea.", "QtWebEngine"))}

    available = set()
    copied_modules = 0
    for module in modules:
        if module.get("type") != "module" or not module.get("path"):
            continue
        path = os.path.realpath(module["path"])
        if not path.startswith(qml_root + os.sep):
            continue
        available.add(module["name"])
        rel = os.path.relpath(path, qml_root)
        for dirpath, dirs, files in os.walk(path):
            # Nested directories are modules of their own and are copied when imported
            dirs[:] = [d for d in dirs if not os.path.exists(os.path.join(dirpath, d, "qmldir"))]
            for f in files:
                if f.endswith((".debug", ".pdb", ".prl")):
                    continue
                src = os.path.join(dirpath, f)
                dst = os.path.join(out, "qml", rel, os.path.relpath(src, path))
                copy_file(src, dst)
        copied_modules += 1
    print("%d QML modules copied" % copied_modules)
    missing = sorted(direct_imports - available)
    if missing:
        raise SystemExit("error: QML modules the app imports are missing from Qt: " + ", ".join(missing))

    # SDL2 here is sdl2-compat, which loads SDL3 by name at run time
    for name in os.listdir(msys_bin):
        if re.match(r"^SDL3.*\.dll$", name, re.I):
            copy_file(os.path.join(msys_bin, name), os.path.join(out, name))

    # Follow the import tables
    def imports(path):
        text = subprocess.run([objdump, "-p", path], check=True, capture_output=True, text=True).stdout
        return [m.group(1) for m in re.finditer(r"DLL Name:\s*(\S+)", text)]

    def find_dll(name):
        for d in search_dirs:
            if not os.path.isdir(d):
                continue
            candidate = os.path.join(d, name)
            if os.path.isfile(candidate):
                return candidate
            for f in os.listdir(d):
                if f.lower() == name.lower():
                    return os.path.join(d, f)
        return None

    queue = []
    for dirpath, _dirs, files in os.walk(out):
        queue += [os.path.join(dirpath, f) for f in files if is_pe(f)]
    scanned = set()
    unknown = {}
    while queue:
        current = queue.pop()
        if current in scanned:
            continue
        scanned.add(current)
        for name in imports(current):
            lower = name.lower()
            if lower in SKIP_DLLS:
                continue
            if os.path.exists(os.path.join(out, name)) or any(
                f.lower() == lower for f in os.listdir(out)
            ):
                continue
            src = find_dll(name)
            if src:
                dst = os.path.join(out, os.path.basename(src))
                copy_file(src, dst)
                queue.append(dst)
            elif not SYSTEM_DLL.match(name):
                unknown.setdefault(name, os.path.relpath(current, out))

    # Windows supplies its own
    for f in os.listdir(out):
        if f.lower().startswith("d3dcompiler"):
            os.remove(os.path.join(out, f))

    if unknown:
        for name, user in sorted(unknown.items()):
            print("error: %s (needed by %s) was not found and is not a known part of Windows" % (name, user))
        raise SystemExit(1)

    total = 0
    count = 0
    for dirpath, _dirs, files in os.walk(out):
        for f in files:
            total += os.path.getsize(os.path.join(dirpath, f))
            count += 1
    print("%d files, %.1f MB in %s" % (count, total / 1e6, out))


if __name__ == "__main__":
    sys.exit(main())
