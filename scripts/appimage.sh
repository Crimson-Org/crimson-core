#!/bin/sh
# Pack a built application into an AppImage in dist/, run from the project folder by its appimage task
#
# quick-sharun bundles every library down to the C library and the DwarFS runtime needs no FUSE,
# so the AppImage starts on any Linux system, and the build host should be Arch Linux

set -eu
version=${VERSION:?VERSION is not set}
architecture=$(uname -m)
repository=${GITHUB_REPOSITORY:-Crimson-Org/$PROGRAM}
staging=$(mktemp -d "${TMPDIR:-/tmp}/$PROGRAM-appimage.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT

sed "s|@EXEC@|$PROGRAM|" "data/$APPLICATION_ID.desktop.in" > "$staging/$APPLICATION_ID.desktop"
export APPDIR="$staging/AppDir"
export DESKTOP="$staging/$APPLICATION_ID.desktop"
export ICON="$PWD/data/icons/scalable/apps/$APPLICATION_ID.svg"
export OUTPATH="$PWD/dist"
export OUTNAME="$PROGRAM-$version-linux-$architecture.AppImage"
export MAIN_BIN="$PROGRAM"
export STARTUPWMCLASS="$APPLICATION_ID"
export GTK_FIX_NONSENSE=1
export ADD_HOOKS="self-updater.hook"

# The updater reads this to fetch only the changed blocks of the newest release
export UPINFO="gh-releases-zsync|${repository%/*}|${repository#*/}|latest|$PROGRAM-*-linux-$architecture.AppImage.zsync"

# quick-sharun patches /usr paths in binaries to /tmp names it picks at random for every build,
# so names taken from the application ID keep unchanged files identical and zsync downloads stay small
path_names=$(printf '%s' "$APPLICATION_ID" | sha256sum)
export _tmp_bin=$(printf '%s' "$path_names" | cut -c 1-3)
export _tmp_lib=$(printf '%s' "$path_names" | cut -c 4-6)
export _tmp_share=$(printf '%s' "$path_names" | cut -c 7-11)

# The packer's own options plus one fixed file time, so builds of the same files match byte for byte
export DWARFS_COMP="zstd:level=22 -S26 -B2 --set-time=0"

quick-sharun "build/$PROGRAM"

# quick-sharun lists the library folders in a random order
sort -o "$APPDIR/lib/lib.path" "$APPDIR/lib/lib.path"

# An installer that manages the AppImage also updates it, so the bundled updater stays quiet there
self_updater="$APPDIR/bin/10-self-updater.hook"
grep -q 'is_cmd --any am ' "$self_updater"
sed -i 's/is_cmd --any am /is_cmd --any appimage-installer am /' "$self_updater"

install -m 644 LICENSE "$APPDIR/"
for pair in ${EXTRA_LICENSES:-}; do
	mkdir -p "$APPDIR/licenses/${pair%%=*}"
	install -m 644 "${pair#*=}" "$APPDIR/licenses/${pair%%=*}/"
done
quick-sharun --make-appimage
