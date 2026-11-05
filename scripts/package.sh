#!/bin/sh
# Pack a built application with its notices into a tar.gz in dist/, run from the project folder by its archive task
#
# PROGRAM and APPLICATION_ID name the application, EXTRA_PROGRAMS lists other built programs to pack with it
# and EXTRA_LICENSES lists name=path pairs for bundled code

set -eu
version=${VERSION:?VERSION is not set}
. "$(dirname "$0")/desktop-files.sh"
name=$PROGRAM-$version-linux-$(uname -m)
staging=$(mktemp -d "${TMPDIR:-/tmp}/$PROGRAM-archive.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
folder=$staging/$name
mkdir -p "$folder"
install -m 755 "build/$PROGRAM" "$folder/"
for extra in ${EXTRA_PROGRAMS:-}; do
	install -m 755 "$extra" "$folder/"
done
install -m 644 README.md "$folder/"
install_licenses "$folder"
install_desktop_files "$folder" "$PROGRAM"
mkdir -p dist
tar -C "$staging" -czf "dist/$name.tar.gz" "$name"
printf '%s\n' "Wrote dist/$name.tar.gz"
