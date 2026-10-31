<h1 align="center">Crimson Core</h1>
<p align="center">The code every Crimson app builds on.</p>

## Why

All Crimson apps start, scroll, save their settings and follow the theme the same way.\
That code lives here once, so a fix reaches every app.

## Features

### Startup

One launch path with the session log, the `--version` flag, the shared stylesheet and the app icon.

### Settings and state

Per app config and state files, and the window size kept between launches.

### Smooth scrolling

Mouse wheel steps glide in either direction, Shift scrolls sideways and touchpads keep their exact motion.

### Sidebar width

A handle on the sidebar's edge drags it wider or narrower, and the app keeps the width for the next launch.

### Long lines

Very long lines show as pieces, so a minified file opens without freezing and saves unchanged.

### Tray

A panel icon shows or hides the window, and its right click menu runs the program's own actions.

### Themes

Crimson Monokai, Blitz, Dracula, Nord, Gruvbox, Catppuccin and Cupcake colour the window, sidebar and title bar of each app.\
They are GtkSourceView style schemes, so a theme saved in `~/.local/share/gtksourceview-5/styles` shows up in all Crimson apps and GtkSourceView editors.\
The `crimson:window`, `crimson:titlebar`, `crimson:sidebar`, `crimson:statusbar` and `crimson:accent` styles colour those parts.

### Loading

A spinner that fades in after 500 ms and a fade for content that arrives.

### Packaging

`scripts/package.sh` packs a built app into a tar.gz, and the reusable nightly workflow publishes a release when `main` changed.

## Building

Steps:
 1. Add the repository as a submodule in `subprojects/crimson-core`
 2. Compile `src/common/*.c` with `-Isubprojects/crimson-core/src` and link GTK4 and libadwaita
 3. Add `assets/common.css` to the app's resources as `common.css`, and compile `assets/crimson-core.gresource.xml` into the program
 4. Give header bars and toolbar views that form the title bar the `title-bar` class
