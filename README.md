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

A handle on the sidebar's edge drags it wider or narrower, and the app keeps the width for the next launch.

### Long lines

Lines over 10,000 characters show as pieces, so a minified file opens without freezing and saves unchanged.

### Loading

A spinner that fades in after 500 ms and a fade for content that arrives.

### Packaging

`scripts/package.sh` packs a built app into a tar.gz, and the reusable nightly workflow builds and publishes a release when `main` changed.

## Building

Add the repository as a submodule in `subprojects/crimson-core`, compile `src/common/*.c` with `-Isubprojects/crimson-core/src` and link GTK4 and libadwaita.\
Add `data/common.css` to the app's resources as `common.css`.
