#!/bin/sh
# Pack a built application with its notices into a tar.gz in dist/, run from the project folder by its archive task
#
# PROGRAM and APPLICATION_ID name the application, and EXTRA_LICENSES lists name=path pairs for bundled code

set -eu
version=${VERSION:?VERSION is not set}
name=$PROGRAM-$version-linux-$(uname -m)
staging=$(mktemp -d "${TMPDIR:-/tmp}/$PROGRAM-archive.XXXXXX")
trap 'rm -rf -- "$staging"' EXIT
folder=$staging/$name
mkdir -p "$folder/share/applications" "$folder/share/icons/hicolor/scalable/apps" "$folder/share/metainfo"
install -m 755 "build/$PROGRAM" "$folder/"
install -m 644 LICENSE README.md "$folder/"
for pair in ${EXTRA_LICENSES:-}; do
	mkdir -p "$folder/licenses/${pair%%=*}"
	install -m 644 "${pair#*=}" "$folder/licenses/${pair%%=*}/"
done
for desktop in data/"$APPLICATION_ID"*.desktop.in; do
	sed "s|@EXEC@|$PROGRAM|" "$desktop" > "$folder/share/applications/$(basename "$desktop" .in)"
done
install -m 644 "data/icons/scalable/apps/$APPLICATION_ID.svg" "$folder/share/icons/hicolor/scalable/apps/"
# Panels find a tray icon's symbolic form only in an installed icon theme, so an application that has one ships it
symbolic=data/icons/symbolic/apps/$APPLICATION_ID-symbolic.svg
if [ -f "$symbolic" ]; then
	mkdir -p "$folder/share/icons/hicolor/symbolic/apps"
	install -m 644 "$symbolic" "$folder/share/icons/hicolor/symbolic/apps/"
fi
install -m 644 "data/$APPLICATION_ID.metainfo.xml" "$folder/share/metainfo/"
mkdir -p dist
tar -C "$staging" -czf "dist/$name.tar.gz" "$name"
printf '%s\n' "Wrote dist/$name.tar.gz"
