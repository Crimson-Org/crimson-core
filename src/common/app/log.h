#pragma once

#include <glib.h>

/* Writes every message, GTK's own warnings included, to logs/ next to the executable when that folder exists */
void start_session_log(const char *slug);
void stop_session_log(void);
/* Whether a session log is being written, so watching that only feeds the log can stay off without one */
gboolean has_session_log(void);
void note_startup_moment(const char *moment);
