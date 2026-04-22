#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
TOOLCHAIN_DIR=${LLVM_TOOLCHAIN_DIR:-"$REPO_ROOT/toolchains/llvm"}
TOOLCHAIN_CONFIG="$TOOLCHAIN_DIR/bin/llvm-config"
TOOLCHAIN_READY="$TOOLCHAIN_DIR/.ready"
ENSURE_SCRIPT="$SCRIPT_DIR/ensure_llvm_toolchain.sh"
HOST_CONFIG=${HOST_LLVM_CONFIG:-llvm-config}

if [ ! -f "$TOOLCHAIN_READY" ] && [ -x "$ENSURE_SCRIPT" ]; then
  if "$ENSURE_SCRIPT" >/dev/null 2>/dev/null; then
    :
  fi
fi

if [ -x "$TOOLCHAIN_CONFIG" ] && [ -f "$TOOLCHAIN_READY" ]; then
  exec "$TOOLCHAIN_CONFIG" "$@"
fi

if command -v "$HOST_CONFIG" >/dev/null 2>&1; then
  exec "$HOST_CONFIG" "$@"
fi

echo "qisc-llvm-config: no vendored LLVM toolchain is ready and host llvm-config was not found" >&2
exit 1
