#!/bin/sh
# Builds and runs the PC-side tests (no devkitPro needed; only a C compiler, zlib and libpng).
# Everything is built with AddressSanitizer and UBSan, into a temporary folder.
#   scripts/host-tests.sh          run all tests
#   scripts/host-tests.sh pxd      run one (tests/test_pxd.c)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CC=${CC:-cc}
PNG_CFLAGS=$(pkg-config --cflags libpng 2>/dev/null || echo "-I/usr/include/libpng16")
F="-Wall -Wextra -Werror -fsanitize=address,undefined -g $PNG_CFLAGS -I $ROOT/source"
S="$ROOT/source"

run() {
	name=$1; shift
	if [ -n "$ONLY" ] && [ "$ONLY" != "$name" ]; then return; fi
	printf '%-8s ' "$name"
	src="$ROOT/tests/test_$name.c"
	[ "$name" = fuzz ] && src="$ROOT/tests/fuzz_load.c"
	$CC $F "$src" "$@" -lpng -lz -lm -o "$OUT/$name"
	(cd "$OUT" && "./$name")
}
ONLY=$1

run pxd     $S/pxd.c $S/doc.c
run layers  $S/pxd.c $S/doc.c
run tools   $S/tools.c $S/doc.c
run select  $S/select.c $S/tools.c $S/export_png.c $S/stamps.c $S/pxd.c $S/doc.c
run text    $S/text.c $S/fonts_data.c
run import  $S/import.c $S/export_png.c $S/pxd.c $S/doc.c
run frames  $S/pxd.c $S/doc.c
run anim    $S/anim.c $S/export_png.c $S/import.c $S/doc.c
run extras  $S/settings.c $S/input.c $S/palette_io.c $S/anim.c $S/export_png.c $S/import.c $S/import_anim.c $S/tools.c $S/doc.c
run fuzz    $S/pxd.c $S/doc.c $S/anim.c $S/export_png.c $S/import_anim.c
