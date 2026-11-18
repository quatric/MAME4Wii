#!/bin/sh
# MAME4Wii build script
#
#   ./build.sh                 build every driver group in groups/ and package
#   ./build.sh classics capcom build only the named groups
#   ./build.sh --list          list the available groups
#   ./build.sh --prepare       only fetch MAME and apply the Wii port
#   ./build.sh --export        copy changes made inside mame/ back into
#                              patches/ and overlay/ (for development)
#
# Environment: DEVKITPRO (default /opt/devkitpro), JOBS (parallel jobs).
#
# Each group becomes its own Homebrew Channel entry (apps/mame4wii-<group>);
# all of them share the data folder sd:/mame4wii (roms, cfg, nvram, ...).

set -e

ROOT=$(cd "$(dirname "$0")" && pwd)
MAME="$ROOT/mame"
DIST="$ROOT/dist"
: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITPPC:=$DEVKITPRO/devkitPPC}"
: "${JOBS:=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
export DEVKITPRO DEVKITPPC

LIBS="libwiiinput libwiiupdate libwiiosk"

die() { echo "error: $*" >&2; exit 1; }

list_groups() {
	for f in "$ROOT"/groups/*.txt; do
		g=$(basename "$f" .txt)
		desc=$(sed -n 's/^# *description: *//p' "$f")
		printf '  %-12s %s\n' "$g" "$desc"
	done
}

prepare() {
	[ -d "$DEVKITPPC" ] || die "devkitPPC not found (set DEVKITPRO)"

	# sources: MAME at the pinned release plus the Wii helper libraries
	if [ ! -f "$MAME/makefile" ] || [ ! -f "$ROOT/3rdparty/libwiiinput/Makefile" ]; then
		git -C "$ROOT" submodule update --init --depth 1 mame
		git -C "$ROOT" submodule update --init 3rdparty/libwiiinput 3rdparty/libwiiupdate 3rdparty/libwiiosk
	fi

	# changes to existing MAME files
	for p in "$ROOT"/patches/*.patch; do
		if git -C "$MAME" apply --reverse --check "$p" 2>/dev/null; then
			: # already applied
		else
			echo "Applying $(basename "$p")"
			git -C "$MAME" apply --whitespace=nowarn "$p"
		fi
	done

	# new files (the Wii OSD, build scripts, boot stub)
	(cd "$ROOT/overlay" && find . -type f) | while read -r f; do
		mkdir -p "$MAME/$(dirname "$f")"
		cmp -s "$ROOT/overlay/$f" "$MAME/$f" || cp "$ROOT/overlay/$f" "$MAME/$f"
	done

	# helper libraries are referenced from MAME's 3rdparty folder
	for l in $LIBS; do
		[ -e "$MAME/3rdparty/$l" ] || ln -s "../../3rdparty/$l" "$MAME/3rdparty/$l"
	done

	make -s -C "$ROOT/3rdparty/libwiiinput" WITH_GCADAPTER=1 WITH_HIDPADS=1
	make -s -C "$ROOT/3rdparty/libwiiupdate"
	make -s -C "$ROOT/3rdparty/libwiiosk"
}

export_changes() {
	[ -f "$MAME/makefile" ] || die "nothing to export: mame/ has not been prepared"
	git -C "$MAME" diff -- . ':!3rdparty/libwiiinput' ':!3rdparty/libwiiupdate' ':!3rdparty/libwiiosk' \
		> "$ROOT/patches/0001-wii-target.patch"
	(cd "$ROOT/overlay" && find . -type f) | while read -r f; do
		cmp -s "$MAME/$f" "$ROOT/overlay/$f" || cp "$MAME/$f" "$ROOT/overlay/$f"
	done
	echo "Updated patches/0001-wii-target.patch and overlay/."
	new=$(git -C "$MAME" status --short --untracked-files=all | sed -n 's/^?? //p' | grep -v '^3rdparty/libwii' |
		while read -r f; do [ -e "$ROOT/overlay/$f" ] || echo "  $f"; done)
	[ -z "$new" ] || printf 'New files not in overlay/ yet (copy them there if they belong to the port):\n%s\n' "$new"
}

build_group() {
	g=$1
	f="$ROOT/groups/$g.txt"
	[ -f "$f" ] || die "unknown group '$g' (see ./build.sh --list)"
	sources=$(grep -v '^[[:space:]]*#' "$f" | grep -v '^[[:space:]]*$' | sed 's|^|src/mame/|' | paste -sd, -)
	desc=$(sed -n 's/^# *description: *//p' "$f")
	target="m4w_$g"

	echo "=== $g: $desc"
	make -C "$MAME" TARGETOS=wii OSD=wii NOWERROR=1 TOOLS=0 OPTIMIZE=s \
		SUBTARGET="$target" SOURCES="$sources" -j"$JOBS"

	app="$DIST/apps/mame4wii-$g"
	mkdir -p "$app"
	make -s -C "$MAME/src/osd/wii/bootstub" PAYLOAD="$MAME/$target.dol" \
		OUTPUT="$app/boot.dol" BUILD="$MAME/build/bootstub/$g"
	cp "$MAME/src/osd/wii/hbc/icon.png" "$app/icon.png"
	sed -e "s|@VERSION@|$VERSION|g" -e "s|@MAMEVER@|$MAMEVER|g" -e "s|@DATE@|$(date +%Y%m%d%H%M%S)|g" \
		-e "s|@SUFFIX@| ($desc)|g" -e "s|@GROUP@|$desc|g" \
		"$MAME/src/osd/wii/hbc/meta.xml.in" > "$app/meta.xml"
	"$DEVKITPPC/bin/powerpc-eabi-size" "$MAME/$target" | tail -1
}

case "$1" in
	--list) echo "Driver groups:"; list_groups; exit 0 ;;
	-h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	--export) export_changes; exit 0 ;;
esac

prepare
[ "$1" = "--prepare" ] && exit 0

VERSION=$(sed -n 's/^#define MAME4WII_VERSION *"\(.*\)"/\1/p' "$MAME/src/osd/wii/wiimain.cpp")
MAMEVER=$(sed -n 's/.*BARE_BUILD_VERSION "\([0-9.]*\)".*/\1/p' "$MAME/makefile" | head -n 1)

if [ $# -eq 0 ]; then
	set -- $(for f in "$ROOT"/groups/*.txt; do basename "$f" .txt; done)
fi
for g in "$@"; do
	build_group "$g"
done

# shared data folder
mkdir -p "$DIST/mame4wii/roms"
cp "$ROOT/overlay/src/osd/wii/hbc/README.txt" "$DIST/mame4wii/README.txt" 2>/dev/null || true
(cd "$DIST" && rm -f "mame4wii-$VERSION.zip" && zip -qr "mame4wii-$VERSION.zip" apps mame4wii)
echo "Done: $DIST/mame4wii-$VERSION.zip"
