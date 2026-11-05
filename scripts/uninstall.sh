#!/bin/sh
# Remove an installed application from the current user, run from the project folder by its uninstall task
#
# PROGRAM, APPLICATION_ID, PREFIX and EXTRA_PROGRAMS are the same as for install.sh

set -eu
prefix=${PREFIX:?PREFIX is not set}
. "$(dirname "$0")/desktop-files.sh"
rm -f "$prefix/bin/$PROGRAM"
for extra in ${EXTRA_PROGRAMS:-}; do
	rm -f "$prefix/bin/$(basename "$extra")"
done
remove_desktop_files "$prefix"
refresh_desktop_caches "$prefix"
printf '%s\n' "Removed $PROGRAM from $prefix"
