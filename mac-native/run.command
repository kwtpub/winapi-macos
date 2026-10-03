#!/usr/bin/env bash
# Двойной клик в Finder открывает Terminal и запускает соседний проект.
set -euo pipefail
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
exec /bin/bash "$SELF_DIR/run.sh" "$@"
