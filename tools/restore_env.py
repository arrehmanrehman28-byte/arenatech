#!/usr/bin/env python3
"""Rebuild the /tmp toolchain prefix after a sandbox reset.

The sandbox snapshot keeps /home/user but wipes /tmp (and excluded build
dirs), so the Qt 6 / FFmpeg / cmake / gtest prefix used by CMake must be
recreated. This script downloads the required Debian trixie .debs via
`apt-get download` (no root needed), expands them into a single rootfs
prefix, rewrites the FFmpeg .pc files to point at the prefix, and emits an
env.sh with the exact export lines the build needs.

Usage:  python3 tools/restore_env.py
Output: /tmp/env/{debs,rootfs,env.sh}
"""
import os
import re
import subprocess
import sys
import urllib.request

BASE = "http://deb.debian.org/debian"
PREFIX = "/tmp/env"
ROOTFS = f"{PREFIX}/rootfs"
DEBS = f"{PREFIX}/debs"

# Seed packages. Runtime libs come in via their dev/metapackage Depends,
# but the components Arena Edit links are named explicitly to be safe.
# NOTE (trixie): Qt runtime packages are libqt6<module>6 (no t64 suffix),
# except core (libqt6core6t64). Verify with `apt-cache search qt6` if the
# suite changes.
SEEDS = [
    "cmake",
    "ninja-build",
    "qt6-base-dev",
    "libqt6core6t64",
    "libqt6gui6",
    "libqt6widgets6",
    "libqt6network6",
    "libqt6concurrent6",
    "libqt6multimedia6",
    "libqt6opengl6",
    "libgtest-dev",
    "libavformat-dev",
    "libavcodec-dev",
    "libavutil-dev",
    "libswscale-dev",
    "libswresample-dev",
    "libproxy1v5",
]


def log(*a):
    print("[restore_env]", *a, flush=True)


def installed_packages():
    out = subprocess.run(
        ["dpkg-query", "-W", "-f=${Package}\n"], capture_output=True, text=True
    ).stdout
    return {l.strip() for l in out.splitlines() if l.strip()}


def fetch_deb(pkg, dest_dir):
    """apt-get download into dest_dir; returns True on success."""
    name = f"{pkg}_"
    for f in os.listdir(dest_dir):
        if f.startswith(name) and f.endswith(".deb"):
            return True
    r = subprocess.run(
        ["apt-get", "download", pkg], cwd=dest_dir, capture_output=True, text=True
    )
    if r.returncode != 0:
        return False
    return any(f.endswith(".deb") for f in os.listdir(dest_dir))


def control_of(deb_path):
    """Return the control member of a .deb via dpkg-deb."""
    r = subprocess.run(
        ["dpkg-deb", "-f", deb_path], capture_output=True, text=True
    )
    if r.returncode != 0:
        raise ValueError(f"dpkg-deb -f failed: {deb_path}")
    return r.stdout


def depends_of(control_text):
    for line in control_text.splitlines():
        if line.startswith("Depends:"):
            return line[len("Depends:") :].strip()
    return ""


def parse_deps(dep_str):
    """Yield concrete candidate names from a Depends string (first
    alternative of each comma-separated item)."""
    out = []
    for item in dep_str.split(","):
        item = item.strip()
        if not item:
            continue
        alt = item.split("|")[0].strip()
        # strip version constraints
        m = re.match(r"([A-Za-z0-9+.-]+)", alt)
        if m:
            out.append(m.group(1))
    return out


def main():
    os.makedirs(DEBS, exist_ok=True)
    os.makedirs(ROOTFS, exist_ok=True)
    have = installed_packages()
    log(f"container already has {len(have)} dpkg packages")

    queue = list(SEEDS)
    seen = set()
    skipped = []
    while queue:
        pkg = queue.pop(0)
        if pkg in seen or pkg in have:
            continue
        seen.add(pkg)
        if not fetch_deb(pkg, DEBS):
            log(f"  skip (not downloadable / virtual): {pkg}")
            skipped.append(pkg)
            continue
        log(f"  fetched {pkg}")
        debs = [f for f in os.listdir(DEBS) if f.startswith(pkg + "_")]
        try:
            ctl = control_of(os.path.join(DEBS, debs[-1]))
            queue.extend(parse_deps(depends_of(ctl)))
        except Exception as e:  # noqa: BLE001
            log(f"  control parse failed for {pkg}: {e}")

    log(f"downloaded {len(seen) - len(skipped)} packages; unresolved: {skipped or 'none'}")

    # Expand everything into the prefix.
    for f in sorted(os.listdir(DEBS)):
        if not f.endswith(".deb"):
            continue
        r = subprocess.run(["dpkg", "-x", os.path.join(DEBS, f), ROOTFS])
        if r.returncode != 0:
            log(f"  dpkg -x FAILED: {f}")

    # Rewrite FFmpeg pkg-config files to point at the prefix.
    pcdir = f"{ROOTFS}/usr/lib/x86_64-linux-gnu/pkgconfig"
    n = 0
    if os.path.isdir(pcdir):
        for f in os.listdir(pcdir):
            if not f.endswith(".pc"):
                continue
            p = os.path.join(pcdir, f)
            s = open(p).read()
            s2 = s.replace("/usr/lib/x86_64-linux-gnu", f"{ROOTFS}/usr/lib/x86_64-linux-gnu")
            s2 = s2.replace("/usr/include/x86_64-linux-gnu", f"{ROOTFS}/usr/include/x86_64-linux-gnu")
            s2 = s2.replace("prefix=/usr", f"prefix={ROOTFS}/usr")
            if s2 != s:
                open(p, "w").write(s2)
                n += 1
    log(f"rewrote {n} .pc files")

    libdir = f"{ROOTFS}/usr/lib/x86_64-linux-gnu"

    # libproxy (Debian) carries an absolute RUNPATH to the backend plugin
    # dir (/usr/lib/x86_64-linux-gnu/libproxy), which does not exist under
    # the prefix. Patch the RUNPATH string in place to $ORIGIN/libproxy
    # (same length, NUL padded) so Qt Network finds libpxbackend-*.so.
    proxy = f"{libdir}/libproxy.so.1"
    if os.path.exists(proxy):
        data = open(proxy, "rb").read()
        old = b"/usr/lib/x86_64-linux-gnu/libproxy"
        new = b"$ORIGIN/libproxy" + b"\x00" * (len(old) - len(b"$ORIGIN/libproxy"))
        if old in data and b"$ORIGIN" not in data:
            open(proxy, "wb").write(data.replace(old, new, 1))
            log("patched libproxy RUNPATH -> $ORIGIN/libproxy")

    # Emit env.sh with the exact export lines.
    libdir = f"{ROOTFS}/usr/lib/x86_64-linux-gnu"
    env = f"""# Generated by tools/restore_env.py — source before configuring/building.
export CMAKE_BIN={ROOTFS}/usr/bin/cmake
export NINJA_BIN={ROOTFS}/usr/bin/ninja
export LD_LIBRARY_PATH={libdir}:/usr/lib/x86_64-linux-gnu
export PKG_CONFIG_PATH={libdir}/pkgconfig
export PKG_CONFIG_LIBDIR={libdir}/pkgconfig
export CMAKE_PREFIX_PATH={libdir}
export Qt6_DIR={libdir}/cmake/Qt6
export QT_PLUGIN_PATH={libdir}/qt6/plugins
export QT_QPA_PLATFORM=offscreen
export GTEST_ROOT={ROOTFS}/usr
export GTEST_INCLUDE_DIR={ROOTFS}/usr/include
export GTEST_LIBRARY={libdir}/libgtest.a
export GTEST_MAIN_LIBRARY={libdir}/libgtest_main.a
"""
    with open(f"{PREFIX}/env.sh", "w") as f:
        f.write(env)
    log(f"wrote {PREFIX}/env.sh")
    log("prefix assembled:")
    for probe in [
        f"{ROOTFS}/usr/bin/cmake",
        f"{libdir}/cmake/Qt6/Qt6Config.cmake",
        f"{libdir}/qt6/plugins/tls",
        f"{libdir}/libavformat.so",
        f"{libdir}/libgtest.a",
        f"{libdir}/libproxy.so.1",
    ]:
        log(f"  {'OK ' if os.path.exists(probe) else 'MISSING'} {probe}")


if __name__ == "__main__":
    main()
