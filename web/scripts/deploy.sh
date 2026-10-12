#!/bin/bash
# deploy.sh - upload the web distribution to Railway from this working tree (web/README.md, "Deploy to Railway").
#
#   web/scripts/deploy.sh [-m "message"]
#
# Stages exactly what web/Dockerfile needs into a temporary folder outside the repository and runs
# `railway up <stage> --path-as-root` for the service bo1-web (the project linked to the repository root:
# `railway link`). The stage holds web/ (without node_modules, dist), mods/, data/ and the prebuilt engine
# web/engine/bo1.{js,wasm,build.json} (`cmake --preset web && cmake --build --preset web`), which .gitignore keeps out
# of a plain `railway up`. The service variable BUILD_ENGINE=prebuilt (.railway/railway.ts) makes the image ship it.
# Without a built engine the script stops (to deploy the site alone, set the service variable BUILD_ENGINE=0).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MESSAGE="deploy $(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo local)"
if [ "${1:-}" = "-m" ] && [ -n "${2:-}" ]; then MESSAGE="$2"; fi

ENGINE="$ROOT/web/engine"
if [ ! -f "$ENGINE/bo1.js" ] || [ ! -f "$ENGINE/bo1.wasm" ]; then
    echo "no engine build in web/engine: cmake --preset web && cmake --build --preset web (docs/web-port.md)" >&2
    exit 1
fi

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/bo1-deploy.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
rsync -a --exclude node_modules --exclude /dist --exclude /engine --exclude /.gitignore "$ROOT/web/" "$STAGE/web/"
mkdir -p "$STAGE/web/engine"
cp "$ENGINE/bo1.js" "$ENGINE/bo1.wasm" "$STAGE/web/engine/"
if [ -f "$ENGINE/build.json" ]; then cp "$ENGINE/build.json" "$STAGE/web/engine/"; fi
rsync -a "$ROOT/mods/" "$STAGE/mods/"
rsync -a "$ROOT/data/" "$STAGE/data/"
printf '%s\n' '**/node_modules' '**/.DS_Store' '**/*.log' 'web/dist' > "$STAGE/.dockerignore"

echo "staged $(du -sh "$STAGE" | cut -f1) in $STAGE"
cd "$ROOT"   # the linked project / environment come from the repository root's link
railway up "$STAGE" --path-as-root --service bo1-web --detach -m "$MESSAGE"
