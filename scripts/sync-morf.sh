#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_BASE="${MORF_SRC_BASE:-$(cd "$ROOT/.." && pwd)}"
resolve_src() {
  local name="$1"
  if [ -d "$SRC_BASE/$name" ]; then echo "$SRC_BASE/$name"; else echo "$SRC_BASE/${name}_travail"; fi
}
sync_one() {
  local name="$1" srcdir="$2" dstdir="$3"
  rm -rf "$dstdir/include" "$dstdir/src"
  cp -r "$srcdir/include" "$dstdir/include"
  cp -r "$srcdir/src"     "$dstdir/src"
  cp    "$srcdir/VERSION" "$dstdir/VERSION"
  echo "OK  $name  ($(cat "$dstdir/VERSION"))"
}
sync_one morfBeacon "$(resolve_src morfBeacon)" "$ROOT/third_party/morf/beacon"
sync_one morfUpdate "$(resolve_src morfUpdate)" "$ROOT/third_party/morf/update"

# morfdeploy : paquet Python (ni include/ ni src/), copie telle quelle depuis le
# depot dedie morfDeploy. Sans cette resynchronisation la copie vendoree derive
# des que morfDeploy evolue -- « morf doctor » le signale, autant l'eviter.
DEPLOY_ROOT="$(resolve_src morfDeploy)"
DEPLOY_SRC="$DEPLOY_ROOT/morfdeploy"
DEPLOY_DST="$ROOT/third_party/morf/morfdeploy"
if [ -d "$DEPLOY_SRC" ]; then
  rm -rf "$DEPLOY_DST"
  mkdir -p "$DEPLOY_DST"
  cp -r "$DEPLOY_SRC/." "$DEPLOY_DST/"
  find "$DEPLOY_DST" -name __pycache__ -type d -prune -exec rm -rf {} +
  [ -f "$DEPLOY_ROOT/VERSION" ] && cp "$DEPLOY_ROOT/VERSION" "$DEPLOY_DST/VERSION"
  echo "OK  morfdeploy  ($(cat "$DEPLOY_DST/VERSION" 2>/dev/null))"
else
  echo "!! Source introuvable pour morfdeploy : $DEPLOY_SRC" >&2
fi

echo "Synchronisation terminee."
