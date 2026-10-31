<h1 align="center">Crimson Core</h1>
<p align="center">The shared C library every Crimson app uses.</p>

## Why

All Crimson apps share this code for starting, scrolling, saving settings and themes.\
A fix here applies to all apps at once.

## About

### Startup

One launch path for the session log, the `--version` flag, the shared stylesheet and the app icon.

### Settings and state

Config and state files for each app, including the window size between launches.

### Smooth scrolling

Mouse wheel steps glide, Shift scrolls sideways and touchpads keep their exact motion.

### Sidebar width

Drag the sidebar edge to resize it, and the app keeps the width for the next launch.

### Long lines

Very long lines show in pieces, so a minified file opens without freezing and saves unchanged.

### Tray

The panel icon shows or hides the window, and its menu runs the app's actions.

### Themes

Built in themes colour the window, sidebar and title bar of each app.\
They are GtkSourceView style schemes, so a theme saved in `~/.local/share/gtksourceview-5/styles` shows up in every Crimson app.\
The `crimson:window`, `crimson:titlebar`, `crimson:sidebar`, `crimson:statusbar` and `crimson:accent` styles colour those parts.

### Loading

A spinner that fades in after half a second, and a fade-in for content when it loads.

## Building

Steps:
 1. Add the repository as a submodule in `subprojects/crimson-core`
 2. Compile `src/common/*.c` with `-Isubprojects/crimson-core/src` and link GTK4 and libadwaita
 3. Add `assets/common.css` to the app's resources as `common.css`, and compile `assets/crimson-core.gresource.xml` into the program
 4. Give header bars and toolbar views that form the title bar the `title-bar` class
