#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
TOOLCHAIN_DIR=${LLVM_TOOLCHAIN_DIR:-"$REPO_ROOT/toolchains/llvm"}
TOOLCHAIN_READY="$TOOLCHAIN_DIR/.ready"
ARCHIVE_PATH=${LLVM_TOOLCHAIN_ARCHIVE:-"$REPO_ROOT/llvm-toolchain.tar.xz"}
DOWNLOAD_URL=${LLVM_TOOLCHAIN_URL:-}
DOWNLOAD_PATH="$REPO_ROOT/.cache/llvm-toolchain-download.tar.xz"

case "$ARCHIVE_PATH" in
  /*) ;;
  *) ARCHIVE_PATH="$REPO_ROOT/$ARCHIVE_PATH" ;;
esac

extract_archive() {
  archive="$1"
  tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/qisc-llvm.XXXXXX")
  trap 'rm -rf "$tmpdir"' EXIT INT TERM

  mkdir -p "$tmpdir"
  tar -xf "$archive" -C "$tmpdir"

  if [ -d "$tmpdir/toolchains/llvm" ]; then
    src="$tmpdir/toolchains/llvm"
  elif [ -d "$tmpdir/qisc-portable/toolchains/llvm" ]; then
    src="$tmpdir/qisc-portable/toolchains/llvm"
  else
    echo "Unable to find toolchains/llvm inside $archive" >&2
    exit 1
  fi

  rm -rf "$TOOLCHAIN_DIR"
  mkdir -p "$(dirname "$TOOLCHAIN_DIR")"
  cp -R "$src" "$TOOLCHAIN_DIR"
}

if [ -f "$TOOLCHAIN_READY" ]; then
  exit 0
fi

if [ -f "$ARCHIVE_PATH" ]; then
  echo "Using local LLVM toolchain archive: $ARCHIVE_PATH" >&2
  extract_archive "$ARCHIVE_PATH"
  exit 0
fi

if [ -n "$DOWNLOAD_URL" ]; then
  mkdir -p "$REPO_ROOT/.cache"
  echo "Fetching LLVM toolchain archive: $DOWNLOAD_URL" >&2
  if command -v curl >/dev/null 2>&1; then
    curl -L "$DOWNLOAD_URL" -o "$DOWNLOAD_PATH"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$DOWNLOAD_PATH" "$DOWNLOAD_URL"
  else
    echo "Neither curl nor wget is available to fetch the LLVM toolchain." >&2
    exit 1
  fi
  extract_archive "$DOWNLOAD_PATH"
  exit 0
fi

exit 1
