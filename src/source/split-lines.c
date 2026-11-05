#include "common/text/long-lines.h"
#include "source/split-lines.h"


static void notice_split_lines(GtkTextBuffer *buffer, GtkTextIter *end, char *text, int length, gpointer mark) {
	if (!has_split_lines(buffer))
		return;
	gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(buffer), NULL);
	gtk_source_buffer_set_highlight_matching_brackets(GTK_SOURCE_BUFFER(buffer), FALSE);
	g_signal_handlers_disconnect_by_data(buffer, mark);
}


void keep_split_lines_plain(GtkSourceBuffer *buffer) {
	static const char WATCH_MARK[] = "keeping-split-lines-plain";
	g_signal_connect_after(buffer, "insert-text", G_CALLBACK(notice_split_lines), (gpointer)WATCH_MARK);
}
