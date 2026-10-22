#pragma once

/* Writes each GLib message to logs/ next to the executable when that folder exists */
void start_session_log(const char *slug);
void stop_session_log(void);
void note_startup_moment(const char *moment);
