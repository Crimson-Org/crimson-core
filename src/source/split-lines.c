#include "common/text/long-lines.h"
#include "source/split-lines.h"


/* The watch stays, since the buffer can be filled again, such as after a reload, which sets the language again */
static void notice_split_lines(GtkTextBuffer *buffer, GtkTextIter *end, char *text, int length, gpointer data) {
	if (!has_split_lines(buffer))
		return;
	gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(buffer), NULL);
	gtk_source_buffer_set_highlight_matching_brackets(GTK_SOURCE_BUFFER(buffer), FALSE);
}


void keep_split_lines_plain(GtkSourceBuffer *buffer) {
	g_signal_connect_after(buffer, "insert-text", G_CALLBACK(notice_split_lines), NULL);
}
