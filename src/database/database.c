#include "database/database.h"

#define BUSY_TIMEOUT_MILLISECONDS 5000

/* With the write-ahead log a crash loses at most the last moments of writing, never the file itself */
static const char OPENING[] =
	"PRAGMA journal_mode = WAL;"
	"PRAGMA synchronous = NORMAL;"
	"PRAGMA foreign_keys = ON;";


static int read_schema_version(sqlite3 *database) {
	sqlite3_stmt *statement = NULL;
	int version = 0;
	if (sqlite3_prepare_v2(database, "PRAGMA user_version;", -1, &statement, NULL) == SQLITE_OK
		&& sqlite3_step(statement) == SQLITE_ROW)
		version = sqlite3_column_int(statement, 0);
	sqlite3_finalize(statement);
	return version;
}


static gboolean has_broken_references(sqlite3 *database) {
	sqlite3_stmt *statement = NULL;
	gboolean is_broken = sqlite3_prepare_v2(database, "PRAGMA foreign_key_check;", -1, &statement, NULL) != SQLITE_OK
		|| sqlite3_step(statement) == SQLITE_ROW;
	sqlite3_finalize(statement);
	return is_broken;
}


/* Every waiting migration goes in one transaction, with foreign keys paused so tables can be rebuilt as SQLite
 * describes, and a failure leaves the database as it was */
static gboolean migrate(sqlite3 *database, const DatabaseLayout *layout, char **failure) {
	int version = read_schema_version(database);
	int newest = (int)layout->migration_count;
	if (version >= newest)
		return TRUE;
	sqlite3_exec(database, "PRAGMA foreign_keys = OFF; BEGIN;", NULL, NULL, NULL);
	gboolean is_done = TRUE;
	for (int index = version; index < newest && is_done; index++)
		is_done = sqlite3_exec(database, layout->migrations[index], NULL, NULL, failure) == SQLITE_OK;
	if (is_done && has_broken_references(database)) {
		*failure = sqlite3_mprintf("%s", "A migration left rows pointing at missing rows");
		is_done = FALSE;
	}
	g_autofree char *ending = g_strdup_printf("PRAGMA user_version = %d; COMMIT;", newest);
	if (is_done)
		is_done = sqlite3_exec(database, ending, NULL, NULL, failure) == SQLITE_OK;
	else
		sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
	sqlite3_exec(database, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
	return is_done;
}


sqlite3 *open_database(const char *path, const DatabaseLayout *layout, GError **error) {
	sqlite3 *database = NULL;
	char *failure = NULL;
	gboolean is_open = sqlite3_open(path, &database) == SQLITE_OK;
	if (is_open) {
		sqlite3_busy_timeout(database, BUSY_TIMEOUT_MILLISECONDS);
		is_open = sqlite3_exec(database, OPENING, NULL, NULL, &failure) == SQLITE_OK
			&& sqlite3_exec(database, layout->schema, NULL, NULL, &failure) == SQLITE_OK
			&& migrate(database, layout, &failure);
	}
	if (is_open)
		return database;
	g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: %s", path,
		failure ? failure : database ? sqlite3_errmsg(database) : "out of memory");
	sqlite3_free(failure);
	sqlite3_close(database);
	return NULL;
}


sqlite3_stmt *prepare_statement(sqlite3 *database, const char *sql) {
	sqlite3_stmt *statement = NULL;
	if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK)
		g_warning("Could not prepare \"%s\": %s", sql, sqlite3_errmsg(database));
	return statement;
}


void bind_text(sqlite3_stmt *statement, int index, const char *text) {
	if (text)
		sqlite3_bind_text(statement, index, text, -1, SQLITE_TRANSIENT);
	else
		sqlite3_bind_null(statement, index);
}


char *read_text_column(sqlite3_stmt *statement, int index) {
	const unsigned char *text = sqlite3_column_text(statement, index);
	return text ? g_strdup((const char *)text) : NULL;
}
