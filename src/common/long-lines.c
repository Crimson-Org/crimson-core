#include <string.h>

#include "long-lines.h"

/* A line is cut into pieces of at most this many characters, which GTK lays out in a few milliseconds */
#define PIECE_CHARACTERS 10000
/* A piece ends after a space or punctuation found this close to its end, or at its full length when there is none */
#define BREAK_SEARCH_CHARACTERS 200
#define SPLIT_TAG "split-line-break"
#define SPLITTING_KEY "splitting-long-lines"


static GtkTextTag *get_split_tag(GtkTextBuffer *buffer) {
	return gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), SPLIT_TAG);
}


static gboolean is_break_after(gunichar character) {
	return g_unichar_isspace(character) || g_unichar_ispunct(character);
}


/* Moves the iterator a piece along the line, back to just after a space or punctuation when one is close */
static void find_piece_end(GtkTextIter *iter) {
	gtk_text_iter_forward_chars(iter, PIECE_CHARACTERS);
	GtkTextIter search = *iter;
	for (int step = 0; step < BREAK_SEARCH_CHARACTERS && gtk_text_iter_backward_char(&search); step++) {
		if (is_break_after(gtk_text_iter_get_char(&search))) {
			gtk_text_iter_forward_char(&search);
			*iter = search;
			return;
		}
	}
}


static void split_line(GtkTextBuffer *buffer, int line) {
	GtkTextIter iter;
	gtk_text_buffer_get_iter_at_line(buffer, &iter, line);
	while (gtk_text_iter_get_chars_in_line(&iter) > PIECE_CHARACTERS + 1) {
		find_piece_end(&iter);
		int offset = gtk_text_iter_get_offset(&iter);
		gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, "\n", 1, SPLIT_TAG, NULL);
		gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset + 1);
	}
}


/* A run of text holds at most as many characters as bytes, so only runs with more bytes than a piece are counted */
static gboolean has_long_run(const char *text, int length, int line_length) {
	const char *run = text, *end = text + length;
	int before = line_length;
	while (run < end) {
		const char *newline = memchr(run, '\n', (size_t)(end - run));
		const char *run_end = newline ? newline : end;
		if (before + (run_end - run) >= PIECE_CHARACTERS)
			return TRUE;
		before = 0;
		run = run_end + 1;
	}
	return FALSE;
}


/* Finds where the incoming text has to be cut so no line grows past a piece, counting from the line it joins,
 */
static GArray *find_cuts(const char *text, int length, int line_length) {
	GArray *cuts = g_array_new(FALSE, FALSE, sizeof(int));
	if (!has_long_run(text, length, line_length))
		return cuts;
	int run = line_length, run_at_break = -1;
	const char *break_after = NULL;
	for (const char *character = text; character < text + length; character = g_utf8_next_char(character)) {
		gunichar value = g_utf8_get_char(character);
		if (value == '\n') {
			run = 0;
			break_after = NULL;
			continue;
		}
		run++;
		const char *next = g_utf8_next_char(character);
		if (is_break_after(value)) {
			break_after = next;
			run_at_break = run;
		}
		if (run < PIECE_CHARACTERS)
			continue;
		gboolean is_break_close = break_after && run - run_at_break < BREAK_SEARCH_CHARACTERS;
		const char *cut = is_break_close ? break_after : next;
		g_array_append_val(cuts, (int){(int)(cut - text)});
		run = is_break_close ? run - run_at_break : 0;
		break_after = NULL;
	}
	return cuts;
}


/* Puts the new text in as pieces joined by marked breaks, so GTK never holds a huge line even for a moment, since
 * every later change to one would count its characters again */
static void cut_incoming_text(GtkTextBuffer *buffer, GtkTextIter *location, char *text, int length, gpointer data) {
	if (g_object_get_data(G_OBJECT(buffer), SPLITTING_KEY))
		return;
	g_autoptr(GArray) cuts = find_cuts(text, length, gtk_text_iter_get_line_offset(location));
	if (!cuts->len)
		return;
	g_signal_stop_emission_by_name(buffer, "insert-text");
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, GINT_TO_POINTER(TRUE));
	int start = 0;
	for (guint index = 0; index <= cuts->len; index++) {
		int end = index < cuts->len ? g_array_index(cuts, int, index) : length;
		gtk_text_buffer_insert(buffer, location, text + start, end - start);
		if (index < cuts->len)
			gtk_text_buffer_insert_with_tags_by_name(buffer, location, "\n", 1, SPLIT_TAG, NULL);
		start = end;
	}
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, NULL);
}


/* Cuts the line the insert ended on when the text after the insert pushed it past a piece, which is short work,
 * since every line already is */
static void notice_insert(GtkTextBuffer *buffer, GtkTextIter *end, char *text, int length, gpointer data) {
	if (g_object_get_data(G_OBJECT(buffer), SPLITTING_KEY)
		|| gtk_text_iter_get_chars_in_line(end) <= PIECE_CHARACTERS + 1)
		return;
	/* The default handler and later ones expect the iterator after the new text, which the breaks move along */
	GtkTextMark *after = gtk_text_buffer_create_mark(buffer, NULL, end, FALSE);
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, GINT_TO_POINTER(TRUE));
	split_line(buffer, gtk_text_iter_get_line(end));
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, NULL);
	gtk_text_buffer_get_iter_at_mark(buffer, end, after);
	gtk_text_buffer_delete_mark(buffer, after);
}


void split_long_lines(GtkTextBuffer *buffer) {
	if (!get_split_tag(buffer))
		gtk_text_buffer_create_tag(buffer, SPLIT_TAG, NULL);
	g_signal_connect(buffer, "insert-text", G_CALLBACK(cut_incoming_text), NULL);
	g_signal_connect_after(buffer, "insert-text", G_CALLBACK(notice_insert), NULL);
}


gboolean has_split_lines(GtkTextBuffer *buffer) {
	GtkTextTag *tag = get_split_tag(buffer);
	if (!tag)
		return FALSE;
	GtkTextIter iter;
	gtk_text_buffer_get_start_iter(buffer, &iter);
	return gtk_text_iter_has_tag(&iter, tag) || gtk_text_iter_forward_to_tag_toggle(&iter, tag);
}


gboolean is_split_break(const GtkTextIter *iter) {
	GtkTextTag *tag = get_split_tag(gtk_text_iter_get_buffer(iter));
	return tag && gtk_text_iter_has_tag(iter, tag);
}


gboolean continues_split_line(const GtkTextIter *iter) {
	GtkTextIter before = *iter;
	gtk_text_iter_set_line_offset(&before, 0);
	return gtk_text_iter_backward_char(&before) && is_split_break(&before);
}


char *get_joined_text(GtkTextBuffer *buffer, const GtkTextIter *start, const GtkTextIter *end) {
	GtkTextTag *tag = get_split_tag(buffer);
	if (!tag)
		return gtk_text_buffer_get_text(buffer, start, end, FALSE);
	GString *text = g_string_new(NULL);
	GtkTextIter piece = *start, next = *start;
	while (gtk_text_iter_compare(&piece, end) < 0) {
		if (gtk_text_iter_has_tag(&piece, tag)) {
			gtk_text_iter_forward_char(&piece);
			continue;
		}
		next = piece;
		if (!gtk_text_iter_forward_to_tag_toggle(&next, tag) || gtk_text_iter_compare(&next, end) > 0)
			next = *end;
		g_autofree char *slice = gtk_text_buffer_get_text(buffer, &piece, &next, FALSE);
		g_string_append(text, slice);
		piece = next;
	}
	return g_string_free(text, FALSE);
}


/* Counts the breaks before the iterator, jumping from break to break through the tag's toggles */
static int count_breaks_before(const GtkTextIter *iter) {
	GtkTextTag *tag = get_split_tag(gtk_text_iter_get_buffer(iter));
	if (!tag)
		return 0;
	GtkTextIter walker;
	gtk_text_buffer_get_start_iter(gtk_text_iter_get_buffer(iter), &walker);
	int count = 0;
	while ((gtk_text_iter_starts_tag(&walker, tag) || gtk_text_iter_forward_to_tag_toggle(&walker, tag))
		&& gtk_text_iter_compare(&walker, iter) < 0) {
		if (gtk_text_iter_starts_tag(&walker, tag))
			count++;
		if (!gtk_text_iter_forward_to_tag_toggle(&walker, tag))
			break;
	}
	return count;
}


int get_joined_line(GtkTextBuffer *buffer, int line) {
	GtkTextIter iter;
	gtk_text_buffer_get_iter_at_line(buffer, &iter, line);
	return line - count_breaks_before(&iter);
}


/* Each break before a line moves it one further down, so the count is taken again until it settles */
void get_iter_at_joined_line(GtkTextBuffer *buffer, GtkTextIter *iter, int line) {
	int buffer_line = line;
	for (;;) {
		gtk_text_buffer_get_iter_at_line(buffer, iter, buffer_line);
		int next = line + count_breaks_before(iter);
		if (next == buffer_line || gtk_text_iter_is_end(iter))
			return;
		buffer_line = next;
	}
}


/* Each break at or before the place pushes it one character further along the buffer */
void get_iter_at_joined_offset(GtkTextBuffer *buffer, GtkTextIter *iter, int offset) {
	gtk_text_buffer_get_iter_at_offset(buffer, iter, offset);
	GtkTextTag *tag = get_split_tag(buffer);
	if (!tag)
		return;
	GtkTextIter walker;
	gtk_text_buffer_get_start_iter(buffer, &walker);
	int buffer_offset = offset;
	while (gtk_text_iter_forward_to_tag_toggle(&walker, tag)) {
		if (!gtk_text_iter_starts_tag(&walker, tag))
			continue;
		if (gtk_text_iter_get_offset(&walker) > buffer_offset)
			break;
		buffer_offset++;
	}
	gtk_text_buffer_get_iter_at_offset(buffer, iter, buffer_offset);
}


void move_to_joined_line_start(GtkTextIter *iter) {
	gtk_text_iter_set_line_offset(iter, 0);
	while (continues_split_line(iter)) {
		gtk_text_iter_backward_line(iter);
		gtk_text_iter_set_line_offset(iter, 0);
	}
}


static void copy_joined(GtkTextView *view, gboolean is_cutting) {
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
	GtkTextIter start, end;
	if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
		return;
	g_autofree char *text = get_joined_text(buffer, &start, &end);
	gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(view)), text);
	if (is_cutting && gtk_text_view_get_editable(view))
		gtk_text_buffer_delete_selection(buffer, TRUE, TRUE);
	g_signal_stop_emission_by_name(view, is_cutting ? "cut-clipboard" : "copy-clipboard");
}


static void copy_selection(GtkTextView *view) {
	if (has_split_lines(gtk_text_view_get_buffer(view)))
		copy_joined(view, FALSE);
}


static void cut_selection(GtkTextView *view) {
	if (has_split_lines(gtk_text_view_get_buffer(view)))
		copy_joined(view, TRUE);
}


void join_long_line_copies(GtkTextView *view) {
	g_signal_connect(view, "copy-clipboard", G_CALLBACK(copy_selection), NULL);
	g_signal_connect(view, "cut-clipboard", G_CALLBACK(cut_selection), NULL);
}
