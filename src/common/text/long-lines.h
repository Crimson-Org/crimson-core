#pragma once

#include <gtk/gtk.h>

/* Cuts every line longer than GTK can lay out quickly into pieces joined by marked breaks, while the file keeps its
 * lines, since one huge line holds the window for seconds each time GTK measures it */
void split_long_lines(GtkTextBuffer *buffer);
/* Copying and cutting in the view leave the marked breaks out */
void join_long_line_copies(GtkTextView *view);

gboolean has_split_lines(GtkTextBuffer *buffer);
/* The character at the iterator is a break between pieces of one file line */
gboolean is_split_break(const GtkTextIter *iter);
/* The iterator's line is a later piece of a file line, which shows no line number of its own */
gboolean continues_split_line(const GtkTextIter *iter);
/* The text as the file holds it, without the marked breaks */
char *get_joined_text(GtkTextBuffer *buffer, const GtkTextIter *start, const GtkTextIter *end);
/* The file's line for a line of the buffer, and the first piece of a file line in the buffer, both counted from 0 */
int get_joined_line(GtkTextBuffer *buffer, int line);
void get_iter_at_joined_line(GtkTextBuffer *buffer, GtkTextIter *iter, int line);
/* The buffer place of a character offset counted in the file's text */
void get_iter_at_joined_offset(GtkTextBuffer *buffer, GtkTextIter *iter, int offset);
/* Moves back to the first piece of the iterator's file line */
void move_to_joined_line_start(GtkTextIter *iter);
