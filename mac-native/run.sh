#!/usr/bin/env bash
# Собрать и запустить проект рядом с модулем.
set -euo pipefail
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
exec /bin/bash "$SELF_DIR/build.sh" --run "$@"
