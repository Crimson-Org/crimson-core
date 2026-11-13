#include <glib/gstdio.h>
#include <jsc/jsc.h>
#include <string.h>

#include "common/app/identity.h"
#include "web/web.h"
#include "youtube/page-rules.h"

#define BUILDER_RESOURCE CORE_RESOURCES "/youtube/rule-builder.js"
#define BUILDER_FUNCTION "buildYouTubeRules"
#define RULES_FILE "youtube-rules.js"
/* uBlock's scriptlet library, listed through GitHub's API so new modules come along by themselves */
#define LIBRARY_LISTING "https://api.github.com/repos/gorhill/uBlock/contents/src/js/resources"
#define LIBRARY_ADDRESS "https://raw.githubusercontent.com/gorhill/uBlock/master/"
#define LIBRARY_FOLDER "src/js/resources/"
#define LISTS_ADDRESS "https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/"
/* The scriptlet modules and the modules outside the library they import */
#define MODULE_NAME_PATTERN "\"name\":\\s*\"([\\w.-]+\\.js)\""
#define OUTSIDE_IMPORT_PATTERN "from\\s+'\\.\\./([\\w.-]+\\.js)'"
#define OUTSIDE_FOLDER "src/js/"
/* Kept rules are built again once they are this old */
#define REFRESH_SECONDS (24 * 60 * 60)

/* The lists that hold uBlock's YouTube rules and the exceptions to them */
static const char *const LISTS[] = {"filters/filters.txt", "filters/quick-fixes.txt", "filters/unbreak.txt"};

/* One refresh, which gathers every file by its path in uBlock's repositories before building */
typedef struct {
	PageRulesReady built;
	gpointer data;
	char *host;
	GHashTable *files;
	guint waiting;
	gboolean has_failed, has_outside_round;
} Refresh;


static char *get_rules_path(void) {
	return g_build_filename(g_get_user_cache_dir(), get_application_identity()->slug, RULES_FILE, NULL);
}


/* Who waits for the kept rules */
typedef struct {
	PageRulesReady ready;
	gpointer data;
} Reading;


static void finish_reading(GObject *file, GAsyncResult *result, gpointer data) {
	g_autofree Reading *reading = data;
	g_autofree char *text = NULL;
	gboolean is_read = g_file_load_contents_finish(G_FILE(file), result, &text, NULL, NULL, NULL);
	reading->ready(is_read ? text : NULL, reading->data);
}


void read_page_rules(PageRulesReady ready, gpointer data) {
	g_autofree char *path = get_rules_path();
	g_autoptr(GFile) file = g_file_new_for_path(path);
	Reading *reading = g_new0(Reading, 1);
	*reading = (Reading){ready, data};
	g_file_load_contents_async(file, NULL, finish_reading, reading);
}


static void free_refresh(Refresh *refresh) {
	g_free(refresh->host);
	g_hash_table_unref(refresh->files);
	g_free(refresh);
}


/* Runs uBlock's library through the builder in a JavaScript context of the worker's own */
static char *build_rules(GHashTable *files, const char *host, GError **error) {
	g_autoptr(JSCContext) context = jsc_context_new();
	g_autoptr(GBytes) builder = g_resources_lookup_data(BUILDER_RESOURCE, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
	g_autoptr(JSCValue) loaded = jsc_context_evaluate(context, g_bytes_get_data(builder, NULL),
		(gssize)g_bytes_get_size(builder));
	g_autoptr(JSCValue) texts = jsc_value_new_object(context, NULL, NULL);
	GHashTableIter iterator;
	gpointer path, text;
	g_hash_table_iter_init(&iterator, files);
	while (g_hash_table_iter_next(&iterator, &path, &text)) {
		g_autoptr(JSCValue) value = jsc_value_new_string(context, text);
		jsc_value_object_set_property(texts, path, value);
	}
	g_autoptr(JSCValue) build = jsc_context_get_value(context, BUILDER_FUNCTION);
	g_autoptr(JSCValue) answer = jsc_value_function_call(build, JSC_TYPE_VALUE, texts, G_TYPE_STRING, host,
		G_TYPE_NONE);
	JSCException *exception = jsc_context_get_exception(context);
	if (exception) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", jsc_exception_get_message(exception));
		return NULL;
	}
	g_autoptr(JSCValue) script = jsc_value_object_get_property(answer, "script");
	g_autoptr(JSCValue) count = jsc_value_object_get_property(answer, "count");
	g_debug("Built %d of uBlock Origin's YouTube rules", jsc_value_to_int32(count));
	return jsc_value_to_string(script);
}


static void build_in_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancellable) {
	Refresh *refresh = data;
	GError *error = NULL;
	char *script = build_rules(refresh->files, refresh->host, &error);
	if (!script) {
		g_task_return_error(task, error);
		return;
	}
	g_autofree char *path = get_rules_path();
	g_autofree char *folder = g_path_get_dirname(path);
	g_mkdir_with_parents(folder, 0700);
	g_autofree char *kept = NULL;
	gboolean is_same = g_file_get_contents(path, &kept, NULL, NULL) && strcmp(kept, script) == 0;
	/* Unchanged rules are written all the same, so their age says when they were last checked */
	g_file_set_contents(path, script, -1, NULL);
	if (is_same)
		g_clear_pointer(&script, g_free);
	g_task_return_pointer(task, script, g_free);
}


static void finish_building(GObject *source, GAsyncResult *result, gpointer data) {
	Refresh *refresh = data;
	g_autoptr(GError) error = NULL;
	g_autofree char *script = g_task_propagate_pointer(G_TASK(result), &error);
	if (error)
		g_warning("uBlock Origin's YouTube rules were not built: %s", error->message);
	refresh->built(script, refresh->data);
	free_refresh(refresh);
}


static void fetch_file(Refresh *refresh, const char *address, const char *path);


/* The library's modules import a few from the folder above it, which are fetched in a second round */
static gboolean fetch_outside_imports(Refresh *refresh) {
	if (refresh->has_outside_round)
		return FALSE;
	refresh->has_outside_round = TRUE;
	g_autoptr(GRegex) pattern = g_regex_new(OUTSIDE_IMPORT_PATTERN, 0, 0, NULL);
	g_autoptr(GHashTable) wanted = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GHashTableIter iterator;
	gpointer path, text;
	g_hash_table_iter_init(&iterator, refresh->files);
	while (g_hash_table_iter_next(&iterator, &path, &text)) {
		g_autoptr(GMatchInfo) match = NULL;
		for (g_regex_match(pattern, text, 0, &match); g_match_info_matches(match); g_match_info_next(match, NULL)) {
			g_autofree char *name = g_match_info_fetch(match, 1);
			g_hash_table_add(wanted, g_strconcat(OUTSIDE_FOLDER, name, NULL));
		}
	}
	g_hash_table_iter_init(&iterator, wanted);
	while (g_hash_table_iter_next(&iterator, &path, NULL)) {
		g_autofree char *address = g_strconcat(LIBRARY_ADDRESS, path, NULL);
		fetch_file(refresh, address, path);
	}
	return g_hash_table_size(wanted) > 0;
}


static void count_arrival(Refresh *refresh) {
	if (--refresh->waiting > 0)
		return;
	if (refresh->has_failed) {
		refresh->built(NULL, refresh->data);
		free_refresh(refresh);
		return;
	}
	if (fetch_outside_imports(refresh))
		return;
	/* The refresh waits for the build untouched, so the worker reads it as it is */
	g_autoptr(GTask) task = g_task_new(NULL, NULL, finish_building, refresh);
	g_task_set_task_data(task, refresh, NULL);
	g_task_run_in_thread(task, build_in_thread);
}


typedef struct {
	Refresh *refresh;
	char *path;
} Arrival;


static void take_file(WebAnswer *answer, gpointer data) {
	g_autofree Arrival *arrival = data;
	if (is_web_answer_ok(answer)) {
		char *text = g_strndup(g_bytes_get_data(answer->body, NULL), g_bytes_get_size(answer->body));
		g_hash_table_insert(arrival->refresh->files, arrival->path, text);
	} else {
		g_warning("uBlock Origin's %s did not come: %s", arrival->path, answer->failure ? answer->failure : "refused");
		arrival->refresh->has_failed = TRUE;
		g_free(arrival->path);
	}
	count_arrival(arrival->refresh);
}


static void fetch_file(Refresh *refresh, const char *address, const char *path) {
	Arrival *arrival = g_new0(Arrival, 1);
	*arrival = (Arrival){refresh, g_strdup(path)};
	refresh->waiting++;
	static const WebRequest REQUEST = {0};
	fetch_web(address, &REQUEST, NULL, take_file, arrival);
}


/* The listing names every module of the library, which come with the lists in one round */
static void take_listing(WebAnswer *answer, gpointer data) {
	Refresh *refresh = data;
	if (!is_web_answer_ok(answer)) {
		g_warning("uBlock Origin's library could not be listed: %s", answer->failure ? answer->failure : "refused");
		refresh->built(NULL, refresh->data);
		free_refresh(refresh);
		return;
	}
	g_autofree char *listing = g_strndup(g_bytes_get_data(answer->body, NULL), g_bytes_get_size(answer->body));
	g_autoptr(GRegex) pattern = g_regex_new(MODULE_NAME_PATTERN, 0, 0, NULL);
	g_autoptr(GMatchInfo) match = NULL;
	/* The lists count as one waiting file until all are asked for, so none can finish the round early */
	refresh->waiting++;
	for (g_regex_match(pattern, listing, 0, &match); g_match_info_matches(match); g_match_info_next(match, NULL)) {
		g_autofree char *name = g_match_info_fetch(match, 1);
		g_autofree char *path = g_strconcat(LIBRARY_FOLDER, name, NULL);
		g_autofree char *address = g_strconcat(LIBRARY_ADDRESS, path, NULL);
		fetch_file(refresh, address, path);
	}
	for (guint index = 0; index < G_N_ELEMENTS(LISTS); index++) {
		g_autofree char *address = g_strconcat(LISTS_ADDRESS, LISTS[index], NULL);
		fetch_file(refresh, address, LISTS[index]);
	}
	count_arrival(refresh);
}


static gboolean is_fresh(void) {
	g_autofree char *path = get_rules_path();
	GStatBuf status;
	return g_stat(path, &status) == 0 && g_get_real_time() / G_USEC_PER_SEC - status.st_mtime < REFRESH_SECONDS;
}


void refresh_page_rules(const char *host, PageRulesReady built, gpointer data) {
	if (is_fresh()) {
		built(NULL, data);
		return;
	}
	Refresh *refresh = g_new0(Refresh, 1);
	*refresh = (Refresh){built, data, g_strdup(host), g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free), 0,
		FALSE, FALSE};
	static const WebRequest REQUEST = {0};
	fetch_web(LIBRARY_LISTING, &REQUEST, NULL, take_listing, refresh);
}
