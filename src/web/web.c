#include <string.h>

#include "common/app/identity.h"
#include "web/web.h"

#define TIMEOUT_SECONDS 30
/* Sites that send a visitor round more redirects than this are going in circles */
#define LARGEST_REDIRECT_COUNT 8
#define DEFAULT_LARGEST_BYTES (16 * 1024 * 1024)

static SoupSession *web;
static SoupCookieJar *cookies;

/* One download on its way, which follows redirects itself so it knows whether each one was permanent */
typedef struct {
	WebRequest request;
	char *etag, *modified;
	GCancellable *cancellable, *own_cancellable;
	gulong cancel_handler;
	WebAnswered answered;
	gpointer data;
	WebAnswer answer;
	guint redirect_count;
	guint64 received;
} Download;


SoupSession *get_web_session(void) {
	if (!web) {
		cookies = soup_cookie_jar_new();
		web = soup_session_new_with_options("timeout", TIMEOUT_SECONDS, "user-agent", get_user_agent(), NULL);
		soup_session_add_feature(web, SOUP_SESSION_FEATURE(cookies));
	}
	return web;
}


void add_web_cookie(const char *name, const char *value, const char *domain) {
	get_web_session();
	SoupCookie *cookie = soup_cookie_new(name, value, domain, "/", SOUP_COOKIE_MAX_AGE_ONE_YEAR);
	soup_cookie_set_secure(cookie, TRUE);
	soup_cookie_jar_add_cookie(cookies, cookie);
}


gboolean is_web_answer_ok(const WebAnswer *answer) {
	return SOUP_STATUS_IS_SUCCESSFUL(answer->status) && answer->body;
}


static void free_download(Download *download) {
	g_free(download->etag);
	g_free(download->modified);
	if (download->cancellable)
		g_cancellable_disconnect(download->cancellable, download->cancel_handler);
	g_clear_object(&download->cancellable);
	g_clear_object(&download->own_cancellable);
	g_clear_pointer(&download->answer.body, g_bytes_unref);
	g_free(download->answer.address);
	g_free(download->answer.type);
	g_free(download->answer.etag);
	g_free(download->answer.modified);
	g_free(download->answer.failure);
	g_free(download);
}


static void finish_download(Download *download) {
	download->answered(&download->answer, download->data);
	free_download(download);
}


static gint64 read_retry_seconds(SoupMessageHeaders *headers) {
	const char *retry = soup_message_headers_get_one(headers, "Retry-After");
	if (!retry)
		return 0;
	if (g_ascii_isdigit(*retry))
		return g_ascii_strtoll(retry, NULL, 10);
	g_autoptr(GDateTime) moment = soup_date_time_new_from_http_string(retry);
	g_autoptr(GDateTime) now = g_date_time_new_now_utc();
	return moment ? MAX(0, g_date_time_difference(moment, now) / G_TIME_SPAN_SECOND) : 0;
}


static void read_headers(Download *download, SoupMessage *message) {
	SoupMessageHeaders *headers = soup_message_get_response_headers(message);
	WebAnswer *answer = &download->answer;
	answer->status = soup_message_get_status(message);
	g_set_str(&answer->type, soup_message_headers_get_content_type(headers, NULL));
	g_set_str(&answer->etag, soup_message_headers_get_one(headers, "ETag"));
	g_set_str(&answer->modified, soup_message_headers_get_one(headers, "Last-Modified"));
	answer->retry_seconds = read_retry_seconds(headers);
}


static void send_request(Download *download);


static gboolean follow_redirect(Download *download, SoupMessage *message) {
	guint status = soup_message_get_status(message);
	const char *location = soup_message_headers_get_one(soup_message_get_response_headers(message), "Location");
	if (!SOUP_STATUS_IS_REDIRECTION(status) || status == SOUP_STATUS_NOT_MODIFIED || !location)
		return FALSE;
	char *next = g_uri_resolve_relative(download->answer.address, location, G_URI_FLAGS_NONE, NULL);
	if (!next || ++download->redirect_count > LARGEST_REDIRECT_COUNT) {
		g_free(next);
		return FALSE;
	}
	gboolean is_permanent = status == SOUP_STATUS_MOVED_PERMANENTLY || status == SOUP_STATUS_PERMANENT_REDIRECT;
	download->answer.has_moved = (download->redirect_count == 1 || download->answer.has_moved) && is_permanent;
	g_free(download->answer.address);
	download->answer.address = next;
	send_request(download);
	return TRUE;
}


static void finish_reading(GObject *source, GAsyncResult *result, gpointer data) {
	Download *download = data;
	g_autoptr(GError) error = NULL;
	GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
	SoupMessage *message = soup_session_get_async_result_message(SOUP_SESSION(source), result);
	if (message && follow_redirect(download, message)) {
		g_clear_pointer(&body, g_bytes_unref);
		return;
	}
	if (message)
		read_headers(download, message);
	if (error && !download->answer.status)
		download->answer.failure = g_strdup(error->message);
	download->answer.body = SOUP_STATUS_IS_SUCCESSFUL(download->answer.status) ? body : NULL;
	if (!download->answer.body && body)
		g_bytes_unref(body);
	finish_download(download);
}


static void check_announced_size(SoupMessage *message, Download *download) {
	goffset length = soup_message_headers_get_content_length(soup_message_get_response_headers(message));
	if (length > (goffset)download->request.largest_bytes)
		g_cancellable_cancel(download->own_cancellable);
}


static void count_received_bytes(SoupMessage *message, guint size, Download *download) {
	download->received += size;
	if (download->received > download->request.largest_bytes)
		g_cancellable_cancel(download->own_cancellable);
}


static void stop_with_caller(GCancellable *cancellable, Download *download) {
	g_cancellable_cancel(download->own_cancellable);
}


static void send_request(Download *download) {
	g_autoptr(SoupMessage) message = soup_message_new(SOUP_METHOD_GET, download->answer.address);
	if (!message) {
		download->answer.failure = g_strdup("The address is not a web address");
		finish_download(download);
		return;
	}
	soup_message_add_flags(message, SOUP_MESSAGE_NO_REDIRECT);
	SoupMessageHeaders *headers = soup_message_get_request_headers(message);
	/* The version only belongs to the address it was named for */
	if (!download->redirect_count && download->etag)
		soup_message_headers_replace(headers, "If-None-Match", download->etag);
	if (!download->redirect_count && download->modified)
		soup_message_headers_replace(headers, "If-Modified-Since", download->modified);
	g_signal_connect(message, "got-headers", G_CALLBACK(check_announced_size), download);
	g_signal_connect(message, "got-body-data", G_CALLBACK(count_received_bytes), download);
	download->received = 0;
	soup_session_send_and_read_async(get_web_session(), message, G_PRIORITY_LOW, download->own_cancellable,
		finish_reading, download);
}


void fetch_web(const char *address, const WebRequest *request, GCancellable *cancellable, WebAnswered answered,
	gpointer data) {
	Download *download = g_new0(Download, 1);
	download->request = request ? *request : (WebRequest){0};
	if (!download->request.largest_bytes)
		download->request.largest_bytes = DEFAULT_LARGEST_BYTES;
	download->etag = g_strdup(download->request.etag);
	download->modified = g_strdup(download->request.modified);
	download->request.etag = download->request.modified = NULL;
	download->answered = answered;
	download->data = data;
	download->answer.address = g_strdup(address);
	download->own_cancellable = g_cancellable_new();
	if (cancellable) {
		download->cancellable = g_object_ref(cancellable);
		download->cancel_handler = g_cancellable_connect(cancellable, G_CALLBACK(stop_with_caller), download, NULL);
	}
	send_request(download);
}
