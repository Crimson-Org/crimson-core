<h1 align="center">Crimson Core</h1>
<p align="center">The code every Crimson app builds on.</p>

## Features

### Startup

One launch path with the session log, the `--version` flag, the shared stylesheet and the app icon.\
Each ten seconds of drawing adds the frame rate, frame time and click delay to the session log.

### Settings and state

Per app config and state files, and the window size kept between launches.

### Smooth scrolling

Mouse wheel steps glide in either direction, Shift scrolls sideways and touchpads keep their exact motion.

### Sidebar width

A handle on the sidebar's edge drags it wider or narrower, and the app keeps the width for the next launch.\
It works with both an overlay and a navigation split view, showing only while the sidebar is docked.

### Long lines

Lines over 10,000 characters show as pieces, so a minified file opens without freezing and saves unchanged.

### Tray

A panel icon shows or hides the window, and its right click menu runs the program's own actions.\
A glowing red dot marks the icon while something waits for the user.

### Themes

Crimson Monokai, Blitz, Dracula, Nord, Gruvbox, Catppuccin and Cupcake colour the window, sidebar and title bar of each app.\
They are GtkSourceView style schemes, so a theme saved in `~/.local/share/gtksourceview-5/styles` shows up in all Crimson apps and GtkSourceView editors.\
The `crimson:titlebar`, `crimson:sidebar` and `crimson:statusbar` styles set those parts, and without them the theme's text background is shaded towards black or white.

### Loading

A spinner that fades in after 500 ms and a fade for content that arrives.

### Packaging

`scripts/package.sh` packs a built app and any extra programs it includes into a tar.gz, and the reusable nightly workflow builds and publishes a release when `main` changed.

## Building

Add the repository as a submodule in `subprojects/crimson-core`, compile `src/common/*.c` with `-Isubprojects/crimson-core/src` and link GTK4 and libadwaita.\
Add `data/common.css` to the app's resources as `common.css`, and compile `data/crimson-core.gresource.xml` into the program for the themes.
