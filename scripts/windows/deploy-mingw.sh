#!/usr/bin/env bash
export PATH="/mingw64/bin:/usr/bin:$PATH"
cd "$1" 2>/dev/null || exit 0
for f in *.exe *.dll */*.dll; do
  [ -f "$f" ] && ldd "$f" 2>/dev/null
done | grep -iE '/mingw64/bin/' | awk '{print $3}' | sort -u | while read -r dll; do
  cp -u "$dll" . 2>/dev/null || true
done
