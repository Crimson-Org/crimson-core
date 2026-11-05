# Shared by the install, uninstall, archive and AppImage scripts, which source it from the project folder
#
# APPLICATION_ID names the application, whose desktop files, icons, metainfo and file types live in assets/, and
# EXTRA_LICENSES lists name=path pairs for bundled code

# Puts the desktop files, icons, metainfo and file types under $1/share, with each desktop file starting $2
install_desktop_files() {
	share=$1/share
	mkdir -p "$share/applications"
	for desktop in assets/"$APPLICATION_ID"*.desktop.in; do
		sed "s|@EXEC@|$2|" "$desktop" > "$share/applications/$(basename "$desktop" .in)"
	done
	install -Dm644 "assets/icons/scalable/apps/$APPLICATION_ID.svg" "$share/icons/hicolor/scalable/apps/$APPLICATION_ID.svg"
	# Panels find a tray icon's symbolic form only in an installed icon theme, so an application that has one ships it
	symbolic=assets/icons/symbolic/apps/$APPLICATION_ID-symbolic.svg
	if [ -f "$symbolic" ]; then
		install -Dm644 "$symbolic" "$share/icons/hicolor/symbolic/apps/$APPLICATION_ID-symbolic.svg"
	fi
	install -Dm644 "assets/$APPLICATION_ID.metainfo.xml" "$share/metainfo/$APPLICATION_ID.metainfo.xml"
	# An application that opens formats the desktop has no file type for ships those types itself
	types=assets/$APPLICATION_ID.mime.xml
	if [ -f "$types" ]; then
		install -Dm644 "$types" "$share/mime/packages/$APPLICATION_ID.xml"
	fi
}

remove_desktop_files() {
	share=$1/share
	for desktop in assets/"$APPLICATION_ID"*.desktop.in; do
		rm -f "$share/applications/$(basename "$desktop" .in)"
	done
	rm -f "$share/icons/hicolor/scalable/apps/$APPLICATION_ID.svg" \
		"$share/icons/hicolor/symbolic/apps/$APPLICATION_ID-symbolic.svg" "$share/metainfo/$APPLICATION_ID.metainfo.xml" \
		"$share/mime/packages/$APPLICATION_ID.xml"
}

# Menus, icon themes and file types read these caches, and systems without the tools have no caches to refresh
refresh_desktop_caches() {
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$1/share/applications"
	fi
	if command -v gtk-update-icon-cache >/dev/null 2>&1; then
		gtk-update-icon-cache --quiet --ignore-theme-index "$1/share/icons/hicolor"
	fi
	if command -v update-mime-database >/dev/null 2>&1 && [ -d "$1/share/mime/packages" ]; then
		update-mime-database "$1/share/mime"
	fi
}

# The application's licence, and the notices of code it bundles under licenses/
install_licenses() {
	install -m 644 LICENSE "$1/"
	for pair in ${EXTRA_LICENSES:-}; do
		mkdir -p "$1/licenses/${pair%%=*}"
		install -m 644 "${pair#*=}" "$1/licenses/${pair%%=*}/"
	done
}
