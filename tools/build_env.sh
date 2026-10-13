#!/bin/sh
# Arena Edit - local (out-of-tree) dependency environment.
#
# All third-party prefixes live in /tmp (workspace snapshot cap is ~128 MB):
#   /tmp/qt6          - Debian trixie Qt 6.8.2 prefix, FLATTENED (no usr/ layer).
#                       Layout: lib/x86_64-linux-gnu/{libs,cmake,qt6/plugins}, include/, bin/.
#   /tmp/ffmpeg-deps  - FFmpeg 7.1.5 prefix (keeps its usr/ layer).
#
# Known patch: /tmp/qt6/lib/x86_64-linux-gnu/libproxy.so.0.5.9 has its RUNPATH
# string patched from /usr/lib/x86_64-linux-gnu/libproxy to $ORIGIN/libproxy
# (Debian ships the backend in a subdirectory and bakes a system RUNPATH; the
# sandbox loader does not resolve our prefix through it). If /tmp/qt6 is ever
# re-extracted from the .debs in /tmp/debs2, re-apply the patch:
#   python3 - <<'EOF'
#   p="/tmp/qt6/lib/x86_64-linux-gnu/libproxy.so.1"
#   d=bytearray(open(p,"rb").read())
#   old=b"/usr/lib/x86_64-linux-gnu/libproxy\0"
#   i=d.find(old); assert i!=-1
#   new=b"$ORIGIN/libproxy\0"
#   d[i:i+len(old)]=new+b"\0"*(len(old)-len(new))
#   open(p,"wb").write(bytes(d))
#   EOF

# Configure (run from repo root):
# PKG_CONFIG_PATH=/tmp/ffmpeg-deps/usr/lib/x86_64-linux-gnu/pkgconfig \
# CMAKE_PREFIX_PATH=/tmp/qt6 \
# cmake -S . -B build -DQt6_DIR=/tmp/qt6/lib/x86_64-linux-gnu/cmake/Qt6 \
#   -DCMAKE_BUILD_TYPE=Release \
#   -DGTEST_ROOT=/tmp/qt6 -DGTEST_INCLUDE_DIR=/tmp/qt6/include \
#   -DGTEST_LIBRARY=/tmp/qt6/lib/x86_64-linux-gnu/libgtest.a \
#   -DGTEST_MAIN_LIBRARY=/tmp/qt6/lib/x86_64-linux-gnu/libgtest_main.a \
#   -DCMAKE_EXE_LINKER_FLAGS="-Wl,--disable-new-dtags -Wl,-rpath-link,/tmp/qt6/lib/x86_64-linux-gnu/libproxy -Wl,-rpath,/tmp/ffmpeg-deps/usr/lib/x86_64-linux-gnu -Wl,-rpath,/tmp/qt6/lib/x86_64-linux-gnu -Wl,-rpath,/tmp/qt6/lib/x86_64-linux-gnu/libproxy" \
#   -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--disable-new-dtags -Wl,-rpath-link,/tmp/qt6/lib/x86_64-linux-gnu/libproxy -Wl,-rpath,/tmp/ffmpeg-deps/usr/lib/x86_64-linux-gnu -Wl,-rpath,/tmp/qt6/lib/x86_64-linux-gnu -Wl,-rpath,/tmp/qt6/lib/x86_64-linux-gnu/libproxy"
#
# Build:  cmake --build build -j2
#
# Run tests (the out-of-tree prefix is not on the system loader path, so
# LD_LIBRARY_PATH is required at run time):
export LD_LIBRARY_PATH=/tmp/qt6/lib/x86_64-linux-gnu:/tmp/ffmpeg-deps/usr/lib/x86_64-linux-gnu
export QT_QPA_PLATFORM=offscreen
export QT_PLUGIN_PATH=/tmp/qt6/lib/x86_64-linux-gnu/qt6/plugins
# (cd build && ctest --output-on-failure)
