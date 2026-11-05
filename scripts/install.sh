#!/bin/sh
# Install a built application for the current user, run from the project folder by its install task
#
# PROGRAM and APPLICATION_ID name the application, PREFIX is where it goes, such as ~/.local, and EXTRA_PROGRAMS
# lists other built programs to install with it

set -eu
prefix=${PREFIX:?PREFIX is not set}
. "$(dirname "$0")/desktop-files.sh"
install -Dm755 "build/$PROGRAM" "$prefix/bin/$PROGRAM"
for extra in ${EXTRA_PROGRAMS:-}; do
	install -Dm755 "$extra" "$prefix/bin/$(basename "$extra")"
done
# The quotes keep a path with spaces in one piece, as desktop files read it
install_desktop_files "$prefix" "\"$prefix/bin/$PROGRAM\""
refresh_desktop_caches "$prefix"
printf '%s\n' "Installed $PROGRAM in $prefix"
