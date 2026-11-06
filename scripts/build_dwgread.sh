#!/usr/bin/env bash
# Builds a statically linked `dwgread` from the pinned LibreDWG release.
#
#   scripts/build_dwgread.sh [OUTPUT_DIR]
#
# The same configure flags are used on every platform so Windows and macOS parse
# files identically:
#   * Windows: run inside an MSYS2 MINGW64 shell (msys2/setup-msys2 in CI).
#   * macOS:   builds arm64 and x86_64 separately and joins them with lipo.
#   * Linux:   native build (used for development and CI tests only).
#
# Output: OUTPUT_DIR/dwgread[.exe] and OUTPUT_DIR/dwgread.version.txt
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build-dwgread}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$OUT/work"
mkdir -p "$WORK"

# shellcheck disable=SC1091
source <(grep -E '^LIBREDWG_[A-Z0-9_]+=' "$ROOT/third_party/libredwg.version")
: "${LIBREDWG_VERSION:?missing in libredwg.version}"
: "${LIBREDWG_URL:?missing in libredwg.version}"
: "${LIBREDWG_SHA256:?missing in libredwg.version}"

# Identical on every platform. --disable-shared + static linking means there are
# no runtime libraries to ship next to the binary.
CONFIGURE_FLAGS=(
  --disable-shared
  --enable-static
  --enable-release
  --disable-bindings
  --disable-python
  --disable-docs
  --disable-werror
)

TARBALL="$WORK/libredwg-$LIBREDWG_VERSION.tar.xz"
if [[ ! -f "$TARBALL" ]]; then
  echo "Downloading $LIBREDWG_URL"
  curl -fsSL --retry 3 -o "$TARBALL" "$LIBREDWG_URL"
fi

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
ACTUAL="$(sha256_of "$TARBALL" | tr 'A-F' 'a-f')"
EXPECTED="$(echo "$LIBREDWG_SHA256" | tr 'A-F' 'a-f')"
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  echo "SHA-256 mismatch for $TARBALL: got $ACTUAL, expected $EXPECTED" >&2
  exit 1
fi

# Fresh, patched source tree.
prepare_source() {
  local dest="$1"
  rm -rf "$dest"
  mkdir -p "$dest"
  tar -xJf "$TARBALL" -C "$dest" --strip-components=1
  shopt -s nullglob
  for p in "$ROOT"/third_party/patches/*.patch; do
    echo "Applying $(basename "$p")"
    patch -d "$dest" -p1 --forward --batch < "$p"
  done
  shopt -u nullglob
}

JOBS="$( (nproc || sysctl -n hw.ncpu || echo 4) 2>/dev/null | head -n1)"

# build_one SRC_DIR BUILD_DIR [extra configure args...]; env CFLAGS/LDFLAGS honoured.
build_one() {
  local src="$1" bld="$2"; shift 2
  rm -rf "$bld"; mkdir -p "$bld"
  (cd "$bld" && "$src/configure" "${CONFIGURE_FLAGS[@]}" "$@")
  make -C "$bld/src" -j"$JOBS"
  make -C "$bld/programs" -j"$JOBS" "dwgread$EXEEXT"
}

EXEEXT=""
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    EXEEXT=".exe"
    prepare_source "$WORK/src"
    CFLAGS="-O2" LDFLAGS="-static -static-libgcc -s" build_one "$WORK/src" "$WORK/build"
    cp "$WORK/build/programs/dwgread.exe" "$OUT/dwgread.exe"
    ;;
  Darwin)
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
    prepare_source "$WORK/src"
    for arch in arm64 x86_64; do
      host="$([[ $arch == arm64 ]] && echo aarch64-apple-darwin || echo x86_64-apple-darwin)"
      CC="clang -arch $arch" CFLAGS="-O2" LDFLAGS="-arch $arch" \
        build_one "$WORK/src" "$WORK/build-$arch" --host="$host"
    done
    lipo -create -output "$OUT/dwgread" \
      "$WORK/build-arm64/programs/dwgread" "$WORK/build-x86_64/programs/dwgread"
    strip -x "$OUT/dwgread" || true
    lipo -info "$OUT/dwgread"
    ;;
  *)
    prepare_source "$WORK/src"
    CFLAGS="-O2" LDFLAGS="-static" build_one "$WORK/src" "$WORK/build" || {
      echo "Fully static link failed; retrying with static libredwg only" >&2
      CFLAGS="-O2" build_one "$WORK/src" "$WORK/build"
    }
    cp "$WORK/build/programs/dwgread" "$OUT/dwgread"
    ;;
esac

"$OUT/dwgread$EXEEXT" --version | tee "$OUT/dwgread.version.txt"
echo "Built $OUT/dwgread$EXEEXT"
