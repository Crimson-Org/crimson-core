#pragma once

#include <jsc/jsc.h>
#include <adwaita.h>
#include <webkit/webkit.h>

/* Player states as YouTube numbers them */
enum {
	YOUTUBE_ENDED = 0,
	YOUTUBE_PLAYING = 1,
	YOUTUBE_PAUSED = 2,
	YOUTUBE_BUFFERING = 3,
};

/* What the YouTube player reports, with times in seconds */
typedef struct {
	const char *video_id;
	int state;
	double time, duration;
} YouTubePlayer;

/* Answers from the page, each called on the main thread */
typedef struct {
	void (*ready)(gboolean is_signed_in, gpointer data);
	/* The message also holds what the app's own script added through describePlayer */
	void (*player_changed)(const YouTubePlayer *player, JSCValue *message, gpointer data);
	/* Every message the shared page does not handle itself, such as comments and the app's own answers */
	void (*message_arrived)(const char *type, JSCValue *message, gpointer data);
	/* An error the app's script reports, with the whole message for its details */
	void (*failed)(const char *text, JSCValue *message, gpointer data);
	/* A media key pressed for the page, "play", "pause", "next" or "previous", which the app answers */
	void (*media_key)(const char *action, gpointer data);
} YouTubePageEvents;

/* Which YouTube the page opens, such as https://music.youtube.com/, and the app's own script on top of the shared one,
 * named as in the app's resources */
typedef struct {
	const char *site;
	const char *start_address;
	const char *script;
	/* The site's name for the log, such as YouTube Music */
	const char *name;
	/* Where a video's own page starts, such as https://www.youtube.com/watch?v=, for a site whose player only lives
	 * there, or NULL for a site whose player is on every page */
	const char *watch_address;
} YouTubeSite;

/* A YouTube page the app drives, which signs in, blocks ads and trackers, answers the app's calls and plays videos */
typedef struct YouTubePage YouTubePage;

/* The page keeps the site and the events, so both must last as long as it does */
YouTubePage *create_youtube_page(const YouTubeSite *site, const YouTubePageEvents *events, gpointer data);
WebKitWebView *get_youtube_page_view(YouTubePage *page);
WebKitNetworkSession *get_youtube_page_session(YouTubePage *page);
/* Loads the blocklist, then the sign in kept from the last launch, then the site */
void start_youtube_page(YouTubePage *page);
void reload_youtube_page(YouTubePage *page);
void sign_out_youtube_page(YouTubePage *page);
/* Runs the function body with the arguments, a vardict that may be NULL and is taken, once the page is ready */
void call_youtube_page(YouTubePage *page, const char *body, GVariant *arguments);
/* A vardict of name and GVariant pairs ended by NULL, where yes and no go as 1 and 0, since WebKit refuses booleans */
GVariant *create_page_arguments(const char *first_name, ...) G_GNUC_NULL_TERMINATED;

/* A text field of a message, or an empty text */
char *read_page_text(JSCValue *object, const char *name);
double read_page_number(JSCValue *object, const char *name);
gboolean read_page_flag(JSCValue *object, const char *name);

/* Starts a video where YouTube remembers it was stopped */
#define YOUTUBE_RESUME (-1.0)
/* Volume runs from 0 to 100, and the video starts at the seconds given, even at 0 when YouTube remembers a later
 * place, or where YouTube remembers with YOUTUBE_RESUME */
void play_youtube_video(YouTubePage *page, const char *video_id, int volume, double start_seconds);
void pause_youtube_video(YouTubePage *page);
void resume_youtube_video(YouTubePage *page);
void seek_youtube_video(YouTubePage *page, double seconds);
void set_youtube_volume(YouTubePage *page, int volume);
/* Stops every sound from the page at once and stops passing its messages on, for a window that is closing */
void silence_youtube_page(YouTubePage *page);
/* Called once the page shows the video alone, so whatever covered it until then can go */
typedef void (*YouTubeVideoDrawn)(gpointer data);
/* Fills the page with the playing video, or shows the page as it was, while the app shows the page itself */
void show_youtube_video(YouTubePage *page, gboolean is_shown, YouTubeVideoDrawn drawn);
/* A video's first comments, or those after a token an earlier answer gave, answered as comments */
void request_youtube_comments(YouTubePage *page, const char *video_id, const char *token);
/* Replies from where a token says they start, answered as replies with the id so they reach the right thread */
void request_youtube_replies(YouTubePage *page, const char *video_id, const char *token, guint id);

/* Shows Google's own sign in page in a dialog with the title, and calls done once it hands over to the site */
void show_youtube_sign_in(YouTubePage *page, GtkWidget *parent, const char *title, GDestroyNotify done, gpointer data);
