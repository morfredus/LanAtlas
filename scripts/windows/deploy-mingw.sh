#!/usr/bin/env bash
export PATH="/ucrt64/bin:/mingw64/bin:/usr/bin:$PATH"
cd "$1" 2>/dev/null || exit 0
TOOLCHAIN_BIN="${2:-}"
if [ -n "$TOOLCHAIN_BIN" ] && [ -d "$TOOLCHAIN_BIN" ]; then
  for f in *.dll; do
    [ -f "$f" ] && [ -f "$TOOLCHAIN_BIN/$f" ] && ! cmp -s "$TOOLCHAIN_BIN/$f" "$f" \
      && cp "$TOOLCHAIN_BIN/$f" "$f"
  done
fi
for f in *.exe *.dll */*.dll; do
  [ -f "$f" ] && ldd "$f" 2>/dev/null
done | grep -iE '/(ucrt64|mingw64)/bin/' | awk '{print $3}' | sort -u | while read -r dll; do
  # Collecte des DLL manquantes (l'alignement des DLL deja la est fait plus haut).
  cp "$dll" . 2>/dev/null || true
done
