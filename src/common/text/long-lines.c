#include <string.h>

#include "common/text/long-lines.h"

/* A line is cut into pieces of at most this many characters, which GTK lays out in a few milliseconds */
#define PIECE_CHARACTERS 10000
/* A piece ends after a space or punctuation found this close to its end, or at its full length when there is none */
#define BREAK_SEARCH_CHARACTERS 200
#define SPLIT_TAG "split-line-break"
#define SPLITTING_KEY "splitting-long-lines"
#define WAS_SPLIT_KEY "long-lines-were-split"
#define OWN_SEPARATORS_KEY "keeps-paragraph-separators"
#define REPLAYING_KEY "replaying-history"
static const char *const HISTORY_SIGNALS[] = {"undo", "redo"};
/* A break is a paragraph separator, which ends a line in GTK as a newline does, but which files almost never hold, so
 * a break that undo, redo or a drag puts back is still known as one. The tag only makes breaks quick to find */
#define BREAK_TEXT " "


static GtkTextTag *get_split_tag(GtkTextBuffer *buffer) {
	return gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), SPLIT_TAG);
}


static gboolean is_break_after(gunichar character) {
	return g_unichar_isspace(character) || g_unichar_ispunct(character);
}


/* Our own changes, and buffers whose file holds paragraph separators of its own, are left as they come */
static gboolean is_left_alone(GtkTextBuffer *buffer) {
	return g_object_get_data(G_OBJECT(buffer), SPLITTING_KEY) || g_object_get_data(G_OBJECT(buffer), OWN_SEPARATORS_KEY);
}


/* Undo and redo put back changes by the places they had, so they must never meet a break added meanwhile */
static gboolean is_replaying(GtkTextBuffer *buffer) {
	return g_object_get_data(G_OBJECT(buffer), REPLAYING_KEY) != NULL;
}


static void start_replaying(GtkTextBuffer *buffer) {
	g_object_set_data(G_OBJECT(buffer), REPLAYING_KEY, GINT_TO_POINTER(TRUE));
}


static void stop_replaying(GtkTextBuffer *buffer) {
	g_object_set_data(G_OBJECT(buffer), REPLAYING_KEY, NULL);
}


static void insert_break(GtkTextBuffer *buffer, GtkTextIter *iter) {
	g_object_set_data(G_OBJECT(buffer), WAS_SPLIT_KEY, GINT_TO_POINTER(TRUE));
	gtk_text_buffer_insert_with_tags_by_name(buffer, iter, BREAK_TEXT, -1, SPLIT_TAG, NULL);
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
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, GINT_TO_POINTER(TRUE));
	while (gtk_text_iter_get_chars_in_line(&iter) > PIECE_CHARACTERS + 1) {
		find_piece_end(&iter);
		int offset = gtk_text_iter_get_offset(&iter);
		insert_break(buffer, &iter);
		gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset + 1);
	}
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, NULL);
}


/* Where the line holding the text ends, at a newline or a break */
static const char *find_line_end(const char *text, const char *end) {
	const char *newline = memchr(text, '\n', (size_t)(end - text));
	const char *found_break = g_strstr_len(text, newline ? newline - text : end - text, BREAK_TEXT);
	return found_break ? found_break : newline;
}


/* A run of text holds at most as many characters as bytes, so only runs with more bytes than a piece are counted */
static gboolean has_long_run(const char *text, int length, int line_length) {
	const char *run = text, *end = text + length;
	int before = line_length;
	while (run < end) {
		const char *line_end = find_line_end(run, end);
		const char *run_end = line_end ? line_end : end;
		if (before + (run_end - run) >= PIECE_CHARACTERS)
			return TRUE;
		before = 0;
		run = line_end ? g_utf8_next_char(line_end) : end;
	}
	return FALSE;
}


/* Finds where the incoming text has to be cut so no line grows past a piece, counting from the line it joins */
static GArray *find_cuts(const char *text, int length, int line_length) {
	GArray *cuts = g_array_new(FALSE, FALSE, sizeof(int));
	if (!has_long_run(text, length, line_length))
		return cuts;
	int run = line_length, run_at_break = -1;
	const char *break_after = NULL;
	for (const char *character = text; character < text + length; character = g_utf8_next_char(character)) {
		gunichar value = g_utf8_get_char(character);
		if (value == '\n' || value == g_utf8_get_char(BREAK_TEXT)) {
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


/* Text that comes back, such as through undo or redo, holds breaks without their tag, which goes back on them */
static void tag_returning_breaks(GtkTextBuffer *buffer, GtkTextMark *after, const char *text, int length) {
	GtkTextIter character;
	gtk_text_buffer_get_iter_at_mark(buffer, &character, after);
	gtk_text_iter_backward_chars(&character, (int)g_utf8_strlen(text, length));
	const char *position = text, *end = text + length, *found;
	while ((found = g_strstr_len(position, end - position, BREAK_TEXT))) {
		gtk_text_iter_forward_chars(&character, (int)g_utf8_strlen(position, found - position));
		int offset = gtk_text_iter_get_offset(&character);
		GtkTextIter next = character;
		gtk_text_iter_forward_char(&next);
		gtk_text_buffer_apply_tag(buffer, get_split_tag(buffer), &character, &next);
		/* Tagging moves no text, but iterators still have to be fetched again after it */
		gtk_text_buffer_get_iter_at_offset(buffer, &character, offset + 1);
		position = found + strlen(BREAK_TEXT);
	}
}


/* Puts in a piece of text, whose breaks are tagged when it came back holding some */
static void insert_piece(GtkTextBuffer *buffer, GtkTextIter *location, const char *text, int length) {
	gtk_text_buffer_insert(buffer, location, text, length);
	if (!g_strstr_len(text, length, BREAK_TEXT))
		return;
	GtkTextMark *after = gtk_text_buffer_create_mark(buffer, NULL, location, FALSE);
	tag_returning_breaks(buffer, after, text, length);
	gtk_text_buffer_get_iter_at_mark(buffer, location, after);
	gtk_text_buffer_delete_mark(buffer, after);
}


/* Puts the new text in as pieces joined by marked breaks, so GTK never holds a huge line even for a moment, since
 * every later change to one would count its characters again */
static void cut_incoming_text(GtkTextBuffer *buffer, GtkTextIter *location, char *text, int length, gpointer data) {
	/* Paragraph separators in a buffer that was never split are the file's own, and they stay, so such a file is
	 * never split, since its separators could not be told from breaks */
	if (!g_object_get_data(G_OBJECT(buffer), WAS_SPLIT_KEY) && g_strstr_len(text, length, BREAK_TEXT))
		g_object_set_data(G_OBJECT(buffer), OWN_SEPARATORS_KEY, GINT_TO_POINTER(TRUE));
	if (is_left_alone(buffer) || is_replaying(buffer))
		return;
	g_autoptr(GArray) cuts = find_cuts(text, length, gtk_text_iter_get_line_offset(location));
	if (!cuts->len)
		return;
	g_signal_stop_emission_by_name(buffer, "insert-text");
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, GINT_TO_POINTER(TRUE));
	int start = 0;
	for (guint index = 0; index <= cuts->len; index++) {
		int end = index < cuts->len ? g_array_index(cuts, int, index) : length;
		insert_piece(buffer, location, text + start, end - start);
		if (index < cuts->len)
			insert_break(buffer, location);
		start = end;
	}
	g_object_set_data(G_OBJECT(buffer), SPLITTING_KEY, NULL);
}


/* Tags breaks that came back, then cuts the line the insert ended on when the text after the insert pushed it past
 * a piece, which is short work, since every line already is */
static void notice_insert(GtkTextBuffer *buffer, GtkTextIter *end, char *text, int length, gpointer data) {
	if (is_left_alone(buffer))
		return;
	gboolean has_breaks = g_strstr_len(text, length, BREAK_TEXT) != NULL;
	if (!has_breaks && gtk_text_iter_get_chars_in_line(end) <= PIECE_CHARACTERS + 1)
		return;
	/* The default handler and later ones expect the iterator after the new text, which the breaks move along */
	GtkTextMark *after = gtk_text_buffer_create_mark(buffer, NULL, end, FALSE);
	if (has_breaks)
		tag_returning_breaks(buffer, after, text, length);
	gtk_text_buffer_get_iter_at_mark(buffer, end, after);
	if (!is_replaying(buffer) && gtk_text_iter_get_chars_in_line(end) > PIECE_CHARACTERS + 1) {
		split_line(buffer, gtk_text_iter_get_line(end));
		gtk_text_buffer_get_iter_at_mark(buffer, end, after);
	}
	gtk_text_buffer_delete_mark(buffer, after);
}


/* Deleting a break joins two pieces, which may make the line too long again */
static void notice_delete(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer data) {
	if (is_left_alone(buffer) || is_replaying(buffer) || gtk_text_iter_get_chars_in_line(start) <= PIECE_CHARACTERS + 1)
		return;
	/* Later handlers expect both iterators at the place of the deleted text */
	GtkTextMark *place = gtk_text_buffer_create_mark(buffer, NULL, start, TRUE);
	split_line(buffer, gtk_text_iter_get_line(start));
	gtk_text_buffer_get_iter_at_mark(buffer, start, place);
	*end = *start;
	gtk_text_buffer_delete_mark(buffer, place);
}


void split_long_lines(GtkTextBuffer *buffer) {
	if (!get_split_tag(buffer))
		gtk_text_buffer_create_tag(buffer, SPLIT_TAG, NULL);
	g_signal_connect(buffer, "insert-text", G_CALLBACK(cut_incoming_text), NULL);
	g_signal_connect_after(buffer, "insert-text", G_CALLBACK(notice_insert), NULL);
	g_signal_connect_after(buffer, "delete-range", G_CALLBACK(notice_delete), NULL);
	for (guint index = 0; index < G_N_ELEMENTS(HISTORY_SIGNALS); index++) {
		g_signal_connect(buffer, HISTORY_SIGNALS[index], G_CALLBACK(start_replaying), NULL);
		g_signal_connect_after(buffer, HISTORY_SIGNALS[index], G_CALLBACK(stop_replaying), NULL);
	}
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
