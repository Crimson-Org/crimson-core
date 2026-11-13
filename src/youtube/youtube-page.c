#include <string.h>

#include "common/app/identity.h"
#include "common/app/log.h"
#include "common/storage/key-store.h"
#include "common/widgets/dialogs.h"
#include "common/widgets/loading.h"
#include "youtube/youtube-page.h"

#define YOUTUBE_RESOURCES CORE_RESOURCES "/youtube/"
#define PAGE_RESOURCE YOUTUBE_RESOURCES "page.js"
#define AD_PRUNER_RESOURCE YOUTUBE_RESOURCES "ad-pruner.js"
#define BLOCKLIST_RESOURCE YOUTUBE_RESOURCES "blocklist.json"
#define MESSAGE_HANDLER "crimson"
/* Followed by a checksum of the rules, so a changed blocklist compiles once and is loaded after that */
#define BLOCKLIST_ID_FORMAT "crimson-blocklist-%s"
#define COOKIE_FILE "cookies.sqlite"
#define CONSENT_COOKIE "SOCS"
#define CONSENT_REJECTED "CAI"
#define CONSENT_DOMAIN ".youtube.com"
#define CONSENT_SECONDS (SOUP_COOKIE_MAX_AGE_ONE_YEAR * 2)
/* A page drawn without the GPU shows video frames as plain green when WebKit hands video to the GPU */
#define CPU_RENDERING_VARIABLE "WEBKIT_SKIA_ENABLE_CPU_RENDERING"
#define GL_VIDEO_OFF_VARIABLE "WEBKIT_GST_DISABLE_GL_SINK"
/* A page that could not load, such as while offline, is tried again after this wait, doubling up to the longest */
#define FIRST_RETRY_SECONDS 2
#define LONGEST_RETRY_SECONDS 120
/* Calls made while the page is not ready wait at most this many, dropping the oldest, so a page that never loads
 * cannot fill the memory */
#define MOST_WAITING_CALLS 100
#define SIGN_IN_FORMAT "https://accounts.google.com/ServiceLogin?service=youtube&continue=%s"
/* Google's page fits whole in this size, and the dialog is smaller only in a smaller window */
#define SIGN_IN_WIDTH 480
#define SIGN_IN_HEIGHT 680

struct YouTubePage {
	const YouTubeSite *site;
	const YouTubePageEvents *events;
	gpointer data;
	WebKitNetworkSession *session;
	WebKitUserContentManager *content;
	WebKitWebView *view;
	gboolean is_ready, is_video_shown;
	/* Requests the page had under way fail as its sign in is cleared, which says nothing worth showing */
	gboolean is_signing_out;
	YouTubeVideoDrawn video_drawn;
	/* Calls made before the page was ready, run in order once it is */
	GPtrArray *waiting_calls;
	guint retry_source, retry_seconds;
};

/* A function body for the page with its arguments, kept until the page can run it */
typedef struct {
	char *body;
	GVariant *arguments;
} PageCall;


static void free_page_call(gpointer data) {
	PageCall *call = data;
	g_free(call->body);
	if (call->arguments)
		g_variant_unref(call->arguments);
	g_free(call);
}


static void finish_page_call(GObject *view, GAsyncResult *result, gpointer data) {
	g_autoptr(GError) error = NULL;
	g_autoptr(JSCValue) value = webkit_web_view_call_async_javascript_function_finish(WEBKIT_WEB_VIEW(view),
		result, &error);
	if (error)
		g_warning("Page call failed: %s", error->message);
}


static void run_page_call(YouTubePage *page, PageCall *call) {
	webkit_web_view_call_async_javascript_function(page->view, call->body, -1, call->arguments, NULL, NULL, NULL,
		finish_page_call, page);
}


void call_youtube_page(YouTubePage *page, const char *body, GVariant *arguments) {
	PageCall *call = g_new0(PageCall, 1);
	call->body = g_strdup(body);
	call->arguments = arguments ? g_variant_ref_sink(arguments) : NULL;
	if (!page->is_ready) {
		if (page->waiting_calls->len >= MOST_WAITING_CALLS)
			g_ptr_array_remove_index(page->waiting_calls, 0);
		g_ptr_array_add(page->waiting_calls, call);
		return;
	}
	run_page_call(page, call);
	free_page_call(call);
}


GVariant *create_page_arguments(const char *first_name, ...) {
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	va_list list;
	va_start(list, first_name);
	for (const char *name = first_name; name; name = va_arg(list, const char *))
		g_variant_builder_add(&builder, "{sv}", name, va_arg(list, GVariant *));
	va_end(list);
	return g_variant_builder_end(&builder);
}


char *read_page_text(JSCValue *object, const char *name) {
	g_autoptr(JSCValue) value = jsc_value_object_get_property(object, name);
	return jsc_value_is_string(value) ? jsc_value_to_string(value) : g_strdup("");
}


double read_page_number(JSCValue *object, const char *name) {
	g_autoptr(JSCValue) value = jsc_value_object_get_property(object, name);
	return jsc_value_is_number(value) ? jsc_value_to_double(value) : 0;
}


gboolean read_page_flag(JSCValue *object, const char *name) {
	g_autoptr(JSCValue) value = jsc_value_object_get_property(object, name);
	return jsc_value_to_boolean(value);
}


static void become_ready(YouTubePage *page, gboolean is_signed_in) {
	g_autofree char *moment = g_strdup_printf("%s page ready", page->site->name);
	note_startup_moment(moment);
	page->is_ready = TRUE;
	page->is_signing_out = FALSE;
	page->retry_seconds = 0;
	for (guint index = 0; index < page->waiting_calls->len; index++)
		run_page_call(page, g_ptr_array_index(page->waiting_calls, index));
	g_ptr_array_set_size(page->waiting_calls, 0);
	page->events->ready(is_signed_in, page->data);
	if (page->is_video_shown)
		show_youtube_video(page, TRUE, page->video_drawn);
}


static void receive_message(WebKitUserContentManager *manager, JSCValue *message, YouTubePage *page) {
	g_autofree char *type = read_page_text(message, "type");
	if (strcmp(type, "ready") == 0) {
		become_ready(page, read_page_flag(message, "signedIn"));
	} else if (strcmp(type, "player") == 0) {
		g_autofree char *video_id = read_page_text(message, "video");
		YouTubePlayer player = {video_id, (int)read_page_number(message, "state"), read_page_number(message, "time"),
			read_page_number(message, "duration")};
		page->events->player_changed(&player, message, page->data);
	} else if (strcmp(type, "media-key") == 0) {
		g_autofree char *action = read_page_text(message, "action");
		page->events->media_key(action, page->data);
	} else if (strcmp(type, "warning") == 0) {
		g_autofree char *text = read_page_text(message, "message");
		g_warning("%s", text);
	} else if (strcmp(type, "error") == 0) {
		g_autofree char *text = read_page_text(message, "message");
		if (!page->is_signing_out)
			page->events->failed(text, message, page->data);
	} else {
		page->events->message_arrived(type, message, page->data);
	}
}


/* A page that loads anew, such as the watch page the first video opens, takes calls again once its script says it
 * is ready, and signed out visitors can land on Google's consent page, where the shared script never runs to say so */
static void notice_page(WebKitWebView *view, WebKitLoadEvent event, YouTubePage *page) {
	const char *address = webkit_web_view_get_uri(view);
	if (event == WEBKIT_LOAD_STARTED)
		page->is_ready = FALSE;
	else if (event == WEBKIT_LOAD_FINISHED && !page->is_ready && !g_str_has_prefix(address, page->site->site))
		become_ready(page, FALSE);
}


static gboolean retry_loading(gpointer data) {
	YouTubePage *page = data;
	page->retry_source = 0;
	webkit_web_view_load_uri(page->view, page->site->start_address);
	return G_SOURCE_REMOVE;
}


/* The page loads again later, waiting longer after each failure until it gets ready, so a page that keeps failing
 * never reloads without pause */
static void retry_later(YouTubePage *page, const char *reason) {
	if (page->retry_source)
		return;
	page->retry_seconds = page->retry_seconds ? MIN(page->retry_seconds * 2, LONGEST_RETRY_SECONDS)
		: FIRST_RETRY_SECONDS;
	g_warning("%s loads again in %u seconds: %s", page->site->name, page->retry_seconds, reason);
	page->retry_source = g_timeout_add_seconds(page->retry_seconds, retry_loading, page);
}


static gboolean notice_load_failure(WebKitWebView *view, WebKitLoadEvent event, char *address, GError *error,
	YouTubePage *page) {
	if (page->is_ready || g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
		return FALSE;
	retry_later(page, error->message);
	return TRUE;
}


/* A page whose web process stopped loads again, and calls wait until it is ready */
static void restart_page(WebKitWebView *view, WebKitWebProcessTerminationReason reason, YouTubePage *page) {
	page->is_ready = FALSE;
	retry_later(page, "the page stopped");
}


/* The page never asks for anything the player needs, so notifications, location and the like are all refused */
static gboolean refuse_permission(WebKitWebView *view, WebKitPermissionRequest *request, gpointer data) {
	webkit_permission_request_deny(request);
	return TRUE;
}


/* The page plays inside the window, so it never takes the whole screen by itself */
static gboolean refuse_full_screen(WebKitWebView *view, gpointer data) {
	return TRUE;
}


static GBytes *read_resource(const char *path) {
	return g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
}


static void add_page_script(YouTubePage *page, const char *path, WebKitUserScriptInjectionTime time) {
	g_autoptr(GBytes) source = read_resource(path);
	g_autofree char *text = g_strndup(g_bytes_get_data(source, NULL), g_bytes_get_size(source));
	g_autofree char *pages = g_strconcat(page->site->site, "*", NULL);
	const char *const allowed[] = {pages, NULL};
	g_autoptr(WebKitUserScript) script = webkit_user_script_new(text, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, time,
		allowed, NULL);
	webkit_user_content_manager_add_script(page->content, script);
}


/* The ad pruner runs before any of the page's own scripts, so YouTube never sees the ads it removes, and the app's
 * script runs after the shared one it builds on */
static void create_content_manager(YouTubePage *page) {
	page->content = webkit_user_content_manager_new();
	add_page_script(page, AD_PRUNER_RESOURCE, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START);
	add_page_script(page, PAGE_RESOURCE, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END);
	g_autofree char *script = get_resource_path(page->site->script);
	add_page_script(page, script, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END);
	webkit_user_content_manager_register_script_message_handler(page->content, MESSAGE_HANDLER, NULL);
	g_signal_connect(page->content, "script-message-received::" MESSAGE_HANDLER, G_CALLBACK(receive_message), page);
}


static char *get_page_path(const char *base, const char *name) {
	return g_build_filename(base, get_application_identity()->slug, "web", name, NULL);
}


YouTubePage *create_youtube_page(const YouTubeSite *site, const YouTubePageEvents *events, gpointer data) {
	YouTubePage *page = g_new0(YouTubePage, 1);
	*page = (YouTubePage){.site = site, .events = events, .data = data};
	/* The web process copies these settings when it starts, so they are set before the first page */
	if (g_getenv(CPU_RENDERING_VARIABLE))
		g_setenv(GL_VIDEO_OFF_VARIABLE, "1", FALSE);
	page->waiting_calls = g_ptr_array_new_with_free_func(free_page_call);
	g_autofree char *data_path = get_page_path(g_get_user_data_dir(), NULL);
	g_autofree char *cache_path = get_page_path(g_get_user_cache_dir(), NULL);
	page->session = webkit_network_session_new(data_path, cache_path);
	create_content_manager(page);
	/* Without this YouTube's player waits for a click before it plays anything */
	g_autoptr(WebKitWebsitePolicies) policies = webkit_website_policies_new_with_policies("autoplay",
		WEBKIT_AUTOPLAY_ALLOW, NULL);
	page->view = g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", page->session,
		"user-content-manager", page->content, "website-policies", policies, NULL);
	g_object_ref_sink(page->view);
	g_signal_connect(page->view, "permission-request", G_CALLBACK(refuse_permission), NULL);
	g_signal_connect(page->view, "enter-fullscreen", G_CALLBACK(refuse_full_screen), NULL);
	g_signal_connect(page->view, "load-changed", G_CALLBACK(notice_page), page);
	g_signal_connect(page->view, "load-failed", G_CALLBACK(notice_load_failure), page);
	g_signal_connect(page->view, "web-process-terminated", G_CALLBACK(restart_page), page);
	return page;
}


WebKitWebView *get_youtube_page_view(YouTubePage *page) {
	return page->view;
}


WebKitNetworkSession *get_youtube_page_session(YouTubePage *page) {
	return page->session;
}


static void open_site(GObject *cookies, GAsyncResult *result, gpointer data) {
	YouTubePage *page = data;
	webkit_cookie_manager_add_cookie_finish(WEBKIT_COOKIE_MANAGER(cookies), result, NULL);
	webkit_web_view_load_uri(page->view, page->site->start_address);
}


/* Visitors who are not signed in meet Google's cookie consent page first, which the shared script never runs on, so
 * the choice to reject the optional cookies is made in advance, as YouTube's consent cookie writes it */
void reload_youtube_page(YouTubePage *page) {
	g_autofree char *moment = g_strdup_printf("Opening %s", page->site->name);
	note_startup_moment(moment);
	page->is_ready = FALSE;
	WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(page->session);
	g_autoptr(SoupCookie) consent = soup_cookie_new(CONSENT_COOKIE, CONSENT_REJECTED, CONSENT_DOMAIN, "/",
		CONSENT_SECONDS);
	soup_cookie_set_secure(consent, TRUE);
	webkit_cookie_manager_add_cookie(cookies, consent, NULL, open_site, page);
}


/* The sign in lives in the app's own cookie store, kept between launches */
static void open_session(YouTubePage *page) {
	note_startup_moment("Blocklist loaded");
	g_autofree char *cookie_path = get_page_path(g_get_user_data_dir(), COOKIE_FILE);
	WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(page->session);
	g_autofree char *directory = g_path_get_dirname(cookie_path);
	make_private_folder(directory);
	webkit_cookie_manager_set_persistent_storage(cookies, cookie_path, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
	reload_youtube_page(page);
}


static void use_blocklist(YouTubePage *page, WebKitUserContentFilter *filter, GError *error) {
	if (filter)
		webkit_user_content_manager_add_filter(page->content, filter);
	else
		g_warning("The ad and tracker blocklist was not loaded: %s", error->message);
	open_session(page);
}


static void finish_compiling_blocklist(GObject *store, GAsyncResult *result, gpointer data) {
	g_autoptr(GError) error = NULL;
	g_autoptr(WebKitUserContentFilter) filter = webkit_user_content_filter_store_save_finish(
		WEBKIT_USER_CONTENT_FILTER_STORE(store), result, &error);
	use_blocklist(data, filter, error);
}


static char *get_blocklist_id(GBytes *rules) {
	g_autofree char *checksum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, rules);
	return g_strdup_printf(BLOCKLIST_ID_FORMAT, checksum);
}


/* Compiling takes about half a second, so it happens only when the stored copy is missing */
static void finish_loading_blocklist(GObject *store, GAsyncResult *result, gpointer data) {
	YouTubePage *page = data;
	g_autoptr(WebKitUserContentFilter) filter = webkit_user_content_filter_store_load_finish(
		WEBKIT_USER_CONTENT_FILTER_STORE(store), result, NULL);
	if (filter) {
		use_blocklist(page, filter, NULL);
		return;
	}
	g_autoptr(GBytes) rules = read_resource(BLOCKLIST_RESOURCE);
	g_autofree char *id = get_blocklist_id(rules);
	webkit_user_content_filter_store_save(WEBKIT_USER_CONTENT_FILTER_STORE(store), id, rules, NULL,
		finish_compiling_blocklist, page);
}


/* The blocklist is in place before the first request, so the page never reaches the ad and tracker hosts */
void start_youtube_page(YouTubePage *page) {
	note_startup_moment("YouTube page starting");
	g_autofree char *store_path = get_page_path(g_get_user_cache_dir(), "filters");
	g_autoptr(WebKitUserContentFilterStore) store = webkit_user_content_filter_store_new(store_path);
	g_autoptr(GBytes) rules = read_resource(BLOCKLIST_RESOURCE);
	g_autofree char *id = get_blocklist_id(rules);
	webkit_user_content_filter_store_load(store, id, NULL, finish_loading_blocklist, page);
}


static void finish_sign_out(GObject *manager, GAsyncResult *result, gpointer data) {
	webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(manager), result, NULL);
	reload_youtube_page(data);
}


void sign_out_youtube_page(YouTubePage *page) {
	page->is_ready = FALSE;
	page->is_signing_out = TRUE;
	webkit_web_view_stop_loading(page->view);
	WebKitWebsiteDataManager *manager = webkit_network_session_get_website_data_manager(page->session);
	webkit_website_data_manager_clear(manager, WEBKIT_WEBSITE_DATA_ALL, 0, NULL, finish_sign_out, page);
}


/* A site whose player only lives on a video's own page opens that page for the first video, and every call waits until
 * it is ready, which later videos never need, since they change in place */
static void open_player(YouTubePage *page, const char *video_id) {
	const char *address = webkit_web_view_get_uri(page->view);
	if (!page->site->watch_address || (address && g_str_has_prefix(address, page->site->watch_address)))
		return;
	page->is_ready = FALSE;
	g_autofree char *escaped = g_uri_escape_string(video_id, NULL, FALSE);
	g_autofree char *watch = g_strconcat(page->site->watch_address, escaped, NULL);
	webkit_web_view_load_uri(page->view, watch);
}


void play_youtube_video(YouTubePage *page, const char *video_id, int volume, double start_seconds) {
	open_player(page, video_id);
	call_youtube_page(page, "return crimsonPage.play(id, volume, start)", create_page_arguments(
		"id", g_variant_new_string(video_id), "volume", g_variant_new_int32(volume),
		"start", g_variant_new_double(start_seconds), NULL));
}


void pause_youtube_video(YouTubePage *page) {
	call_youtube_page(page, "crimsonPage.pause()", NULL);
}


void resume_youtube_video(YouTubePage *page) {
	call_youtube_page(page, "crimsonPage.resume()", NULL);
}


void seek_youtube_video(YouTubePage *page, double seconds) {
	call_youtube_page(page, "crimsonPage.seek(seconds)", create_page_arguments("seconds", g_variant_new_double(seconds),
		NULL));
}


void set_youtube_volume(YouTubePage *page, int volume) {
	call_youtube_page(page, "crimsonPage.setVolume(volume)", create_page_arguments("volume",
		g_variant_new_int32(volume), NULL));
}


/* Muting takes effect at once, where pausing waits for the page's script, and the page's later reports no longer
 * reach the window that is closing */
void silence_youtube_page(YouTubePage *page) {
	webkit_web_view_set_is_muted(page->view, TRUE);
	pause_youtube_video(page);
	g_signal_handlers_disconnect_by_data(page->content, page);
}


static void finish_video_call(GObject *view, GAsyncResult *result, gpointer data) {
	YouTubePage *page = data;
	g_autoptr(JSCValue) value = webkit_web_view_call_async_javascript_function_finish(WEBKIT_WEB_VIEW(view), result,
		NULL);
	if (page->is_video_shown && page->video_drawn)
		page->video_drawn(page->data);
}


void show_youtube_video(YouTubePage *page, gboolean is_shown, YouTubeVideoDrawn drawn) {
	page->is_video_shown = is_shown;
	page->video_drawn = drawn;
	if (!page->is_ready)
		return;
	g_autoptr(GVariant) arguments = g_variant_ref_sink(create_page_arguments("shown", g_variant_new_int32(is_shown),
		NULL));
	webkit_web_view_call_async_javascript_function(page->view, "return crimsonPage.showVideo(shown)", -1, arguments,
		NULL, NULL, NULL, finish_video_call, page);
}


void request_youtube_comments(YouTubePage *page, const char *video_id, const char *token) {
	call_youtube_page(page, "return crimsonPage.loadComments(video, token)", create_page_arguments(
		"video", g_variant_new_string(video_id), "token", g_variant_new_string(token ? token : ""), NULL));
}


void request_youtube_replies(YouTubePage *page, const char *video_id, const char *token, guint id) {
	call_youtube_page(page, "return crimsonPage.loadReplies(video, token, id)", create_page_arguments(
		"video", g_variant_new_string(video_id), "token", g_variant_new_string(token),
		"id", g_variant_new_int32((int)id), NULL));
}


/* The dialog and who to tell when the sign in is through */
typedef struct {
	AdwDialog *dialog;
	const char *site;
	GDestroyNotify done;
	gpointer data;
	gboolean is_finished;
} SignIn;


static void notice_sign_in_page(WebKitWebView *view, WebKitLoadEvent event, SignIn *sign_in) {
	if (event != WEBKIT_LOAD_COMMITTED || sign_in->is_finished
		|| !g_str_has_prefix(webkit_web_view_get_uri(view), sign_in->site))
		return;
	sign_in->is_finished = TRUE;
	webkit_web_view_stop_loading(view);
	sign_in->done(sign_in->data);
	adw_dialog_force_close(sign_in->dialog);
}


/* Google's page stays hidden behind the waiting spinner until its first load is through, then fades in, so the
 * dialog never shows a blank page */
static void show_first_page(WebKitWebView *view, WebKitLoadEvent event, GtkWidget *spinner) {
	if (event != WEBKIT_LOAD_FINISHED || !gtk_widget_get_visible(spinner))
		return;
	gtk_widget_set_visible(spinner, FALSE);
	fade_in_widget(GTK_WIDGET(view));
}


void show_youtube_sign_in(YouTubePage *page, GtkWidget *parent, const char *title, GDestroyNotify done, gpointer data) {
	SignIn *sign_in = g_new0(SignIn, 1);
	*sign_in = (SignIn){adw_dialog_new(), page->site->site, done, data, FALSE};
	adw_dialog_set_title(sign_in->dialog, title);
	adw_dialog_set_content_width(sign_in->dialog, SIGN_IN_WIDTH);
	fit_dialog_height(sign_in->dialog, parent, SIGN_IN_HEIGHT);
	GtkWidget *view = g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", page->session, NULL);
	gtk_widget_set_vexpand(view, TRUE);
	gtk_widget_set_opacity(view, 0);
	g_signal_connect(view, "load-changed", G_CALLBACK(notice_sign_in_page), sign_in);
	GtkWidget *spinner = create_loading_spinner();
	g_signal_connect(view, "load-changed", G_CALLBACK(show_first_page), spinner);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(overlay), view);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), spinner);
	GtkWidget *toolbar = adw_toolbar_view_new();
	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());
	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), overlay);
	adw_dialog_set_child(sign_in->dialog, toolbar);
	g_object_set_data_full(G_OBJECT(sign_in->dialog), "sign-in", sign_in, g_free);
	g_autofree char *site = g_uri_escape_string(page->site->site, NULL, FALSE);
	g_autofree char *address = g_strdup_printf(SIGN_IN_FORMAT, site);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), address);
	adw_dialog_present(sign_in->dialog, parent);
}
