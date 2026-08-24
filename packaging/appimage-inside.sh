#!/usr/bin/env bash
# Runs INSIDE the fedora:40 container. /src is the project checkout.
set -euo pipefail

echo "==> installing build deps"
dnf install -y --setopt=install_weak_deps=False \
    cmake gcc-c++ git-core file \
    qt6-qtbase-devel qt6-qtwayland \
    wget curl tar gzip >/dev/null

VERSION=$(grep -oP '(?<=project\(tuxnotes VERSION )\S+' CMakeLists.txt)
echo "==> version ${VERSION}"

echo "==> cmake build"
cmake -B build-appimage -S . -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-appimage -j"$(nproc)" >/dev/null

APPDIR=/work/AppDir
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr"
DESTDIR="$APPDIR" cmake --install build-appimage --prefix /usr

# ---- AppImage-required metadata at AppDir root -----------------------------
cp /src/icon.png "$APPDIR/tuxnotes.png"
cat > "$APPDIR/tuxnotes.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=TuxNotes
GenericName=Sticky Notes
Comment=Post-it style notes that live on your desktop
Exec=tuxnotes
Icon=tuxnotes
Categories=Utility;
Actions=new;

[Desktop Action new]
Name=New Note
Exec=tuxnotes --new-note
Icon=document-new
EOF

# ---- Bundle Qt libraries + plugins manually --------------------------------
QT_LIBS=$(qmake6 -query QT_INSTALL_LIBS)       # /usr/lib64
QT_PLUGINS=$(qmake6 -query QT_INSTALL_PLUGINS) # /usr/lib64/qt6/plugins

mkdir -p "$APPDIR/usr/lib" "$APPDIR/usr/plugins"

# Libraries never bundled: glibc/toolchain (host provides) and GL stack
# (host driver must win — bundling Mesa breaks proprietary setups).
EXCLUDE='^(ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libgcc_s|libstdc\+\+|libGL|libEGL|libOpenGL|libgbm|libdrm|libglapi|libvulkan)'

copy_with_deps() {
    local f="$1"
    local base
    base=$(basename "$f")
    [[ $base =~ ^($EXCLUDE) ]] && return 0
    [[ -e "$APPDIR/usr/lib/$base" ]] && return 0
    cp -L "$f" "$APPDIR/usr/lib/$base"
    # recurse over this library's own dependencies
    for dep in $(ldd "$APPDIR/usr/lib/$base" | awk '{print $3}' | grep '^/'); do
        copy_with_deps "$dep"
    done
}

echo "==> bundling libraries (recursive ldd walk)"
for f in $(ldd "$APPDIR/usr/bin/tuxnotes" | awk '{print $3}' | grep '^/'); do
    copy_with_deps "$f"
done

echo "==> bundling Qt plugins"
for pdir in platforms imageformats iconengines styles tls \
            wayland-shell-integration wayland-graphics-integration-client; do
    [[ -d "$QT_PLUGINS/$pdir" ]] || continue
    mkdir -p "$APPDIR/usr/plugins/$pdir"
    for so in "$QT_PLUGINS/$pdir"/*.so; do
        [[ -e "$so" ]] || continue
        base=$(basename "$so")
        [[ "$base" == libqxcb* && "$pdir" == platforms ]] || true
        cp "$so" "$APPDIR/usr/plugins/$pdir/"
        for f in $(ldd "$APPDIR/usr/plugins/$pdir/$base" | awk '{print $3}' | grep '^/'); do
            copy_with_deps "$f"
        done
    done
done
# Drop X11-only platform bits when Wayland files exist — keeps size down but
# harmless either way; keep both for portability across hosts.

cat > "$APPDIR/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix=..
Plugins=../plugins
Libraries=../lib
EOF

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
exec "$HERE/usr/bin/tuxnotes" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "==> verifying dynamic deps"
MISSING=$(find "$APPDIR/usr" \( -name '*.so*' -o -name tuxnotes \) -print0 | while IFS= read -r -d '' f; do
    ldd "$f" 2>/dev/null | grep "not found" || true
done | sort -u)
if [[ -n "${MISSING:-}" ]]; then
    echo "MISSING LIBS:"; echo "$MISSING"; exit 1
fi
echo "deps ok"

mkdir -p /src/dist
OUT="/src/dist/tuxnotes-${VERSION}-x86_64.AppImage"
rm -f "$OUT"

echo "==> fetching appimagetool"
cd /work
ARCH=x86_64
curl -fsSL -o at.AppImage "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
    || wget -q -O at.AppImage "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage"
chmod +x at.AppImage

echo "==> creating squashfs AppImage"
./at.AppImage --appimage-extract-and-run "$APPDIR" "$OUT" 2>&1 | tail -2
chmod +x "$OUT"

echo "==> AppImage at ${OUT}"
