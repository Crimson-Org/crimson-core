#pragma once

#include <glib.h>
#include <sqlite3.h>

/* What a program's database looks like: the first shape of its tables, then each later change in order */
typedef struct {
	const char *schema;
	const char *const *migrations;
	guint migration_count;
} DatabaseLayout;

/* Opens or makes the database with a write-ahead log and foreign keys, brings its tables up to date, and says why
 * when it could not */
sqlite3 *open_database(const char *path, const DatabaseLayout *layout, GError **error);
/* Prepares a statement and logs the reason when the SQL is wrong, which is a mistake in the program */
sqlite3_stmt *prepare_statement(sqlite3 *database, const char *sql);
/* Binds text that may be NULL, which then binds as SQL NULL */
void bind_text(sqlite3_stmt *statement, int index, const char *text);
/* A text column as a new string, or NULL for SQL NULL */
char *read_text_column(sqlite3_stmt *statement, int index);
