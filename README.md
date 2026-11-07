<h1 align="center">Crimson Core</h1>
<p align="center">The shared C library every Crimson app uses.</p>

## Why

All Crimson apps share this code for starting, scrolling, settings and themes.\
So every app starts, scrolls and looks the same.

## About

### Startup

One launch path for the session log, the `--version` flag, the shared stylesheet, the app icon, and the About, Settings and Quit actions with their shortcuts.

### Settings and state

Config and state files for each app.

### Smooth scrolling

Mouse wheel steps glide, Shift scrolls sideways and touchpads keep their exact motion.

### Long lines

Very long lines show in pieces, so a minified file opens without freezing and saves unchanged.

### Tray

The panel icon shows or hides the window, and the icon's menu runs the app's actions.

### Themes

Built in themes colour the window, sidebar and title bar of the apps that use them.\
They are GtkSourceView style schemes, so a theme saved in `~/.local/share/gtksourceview-5/styles` shows up in each of them.\
The `crimson:window`, `crimson:titlebar`, `crimson:sidebar`, `crimson:statusbar` and `crimson:accent` styles colour those parts.

### Loading

A spinner that waits half a second before fading in, and content that fades in once loaded.

### Code views

Line numbers that count the file's own lines, for apps built on GtkSourceView.

### Installing

Shared scripts that install, remove and pack an app with its desktop files, icons and notices.

## Building

Steps:
 1. Add the repository as a submodule in `subprojects/crimson-core`
 2. Compile `src/common/*/*.c` with `-Isubprojects/crimson-core/src` and link GTK4 and libadwaita
 3. For an app built on GtkSourceView, also compile `src/source/*.c`
 4. Compile `assets/crimson-core.gresource.xml` into the program
 5. Give header bars and toolbar views that form the title bar the `title-bar` class
