#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(head -n1 "$ROOT/VERSION" | tr -d '[:space:]')"
BINARY=""
for dir in "$ROOT/build" "$ROOT/build-mingw"; do
  if [ -x "$dir/LanAtlas" ]; then BINARY="$dir/LanAtlas"; break; fi
done
[ -n "$BINARY" ] || { echo "compiler d'abord (cmake --preset linux)"; exit 1; }
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/lanatlas_${VERSION}_${ARCH}"
mkdir -p "$PKG/usr/bin" "$PKG/usr/share/doc/lanatlas" "$PKG/DEBIAN"
cp "$BINARY" "$PKG/usr/bin/lanatlas"
cp "$ROOT/README.md" "$ROOT/README.fr.md" "$ROOT/CHANGELOG.md" "$ROOT/LICENSE" "$PKG/usr/share/doc/lanatlas/" 2>/dev/null || true
cat > "$PKG/DEBIAN/control" <<EOF
Package: lanatlas
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCH
Maintainer: morfredus <morfredus@users.noreply.github.com>
Depends: libqt6widgets6, libqt6network6
Description: Cartographie du reseau local (Livebox + mesh Deco)
EOF
mkdir -p "$ROOT/dist"
dpkg-deb --build "$PKG" "$ROOT/dist/lanatlas_${VERSION}_${ARCH}.deb"
echo "OK $ROOT/dist/lanatlas_${VERSION}_${ARCH}.deb"
