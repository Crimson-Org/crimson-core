#include <gtk/gtk.h>

#include "frame-rate.h"

/* Each summary covers at least this much time spent drawing */
#define SUMMARY_MICROSECONDS (10 * G_USEC_PER_SEC)
/* A longer pause between frames is the window resting, not a slow frame */
#define REST_MICROSECONDS (250 * G_TIME_SPAN_MILLISECOND)
/* A gap this many refreshes long means the screen showed the same picture again at least once */
#define MISSED_REFRESH_SHARE 1.5
/* Work longer than this many microseconds in one frame is counted as heavy, since 120 Hz leaves 8.3 ms */
#define HEAVY_WORK_MICROSECONDS 8000
/* A click or key whose result takes longer than this to reach the screen is written down on its own */
#define SLOW_RESPONSE_MICROSECONDS (100 * G_TIME_SPAN_MILLISECOND)

/* The window checks in this often, and a check this late means it could not answer or draw in the meantime */
#define HEARTBEAT_MILLISECONDS 100
#define HELD_MICROSECONDS (250 * 1000)

/* What one window has drawn since its last summary */
typedef struct {
	gint64 paint_start, last_frame_time, drawing_time;
	gint64 work_time, heaviest_work, slowest_gap, refresh_interval;
	guint frames, missed_refreshes, heavy_frames;
	/* When the oldest click or key still waiting for a frame came in, and how long answers took */
	gint64 input_time, response_time, slowest_response;
	guint responses;
} FrameRecord;


static double to_milliseconds(gint64 microseconds) {
	return microseconds / (double)G_TIME_SPAN_MILLISECOND;
}


static void write_summary(FrameRecord *record) {
	double seconds = (double)record->drawing_time / G_USEC_PER_SEC;
	double refresh_rate = record->refresh_interval ? (double)G_USEC_PER_SEC / record->refresh_interval : 0;
	double response = record->responses ? to_milliseconds(record->response_time) / record->responses : 0;
	g_debug("Frames %u in %.1f s of drawing at %.1f fps on a %.0f Hz screen, %u refreshes missed, "
		"slowest gap %.1f ms, work %.2f ms on average and %.2f ms at most, %u heavy frames, "
		"%u clicks and keys answered in %.1f ms on average and %.1f ms at most",
		record->frames, seconds, record->frames / seconds, refresh_rate, record->missed_refreshes,
		to_milliseconds(record->slowest_gap), to_milliseconds(record->work_time) / record->frames,
		to_milliseconds(record->heaviest_work), record->heavy_frames, record->responses, response,
		to_milliseconds(record->slowest_response));
	*record = (FrameRecord){.last_frame_time = record->last_frame_time, .input_time = record->input_time};
}


/* The frame after a click or key is the first that can show its result */
static void note_response(FrameRecord *record) {
	gint64 response = g_get_monotonic_time() - record->input_time;
	record->input_time = 0;
	record->responses++;
	record->response_time += response;
	record->slowest_response = MAX(record->slowest_response, response);
	if (response > SLOW_RESPONSE_MICROSECONDS)
		g_debug("Slow answer to a click or key, %.1f ms until the next frame", to_milliseconds(response));
}


/* Called before updating, layout and drawing, which together are the frame's work on the main thread */
static void start_frame(GdkFrameClock *clock, FrameRecord *record) {
	record->paint_start = g_get_monotonic_time();
	gint64 frame_time = gdk_frame_clock_get_frame_time(clock);
	gint64 gap = frame_time - record->last_frame_time;
	record->last_frame_time = frame_time;
	if (gap <= 0 || gap > REST_MICROSECONDS)
		return;
	gdk_frame_clock_get_refresh_info(clock, frame_time, &record->refresh_interval, NULL);
	record->drawing_time += gap;
	record->slowest_gap = MAX(record->slowest_gap, gap);
	if (record->refresh_interval && gap > record->refresh_interval * MISSED_REFRESH_SHARE)
		record->missed_refreshes += (guint)(gap / record->refresh_interval) - 1;
}


static void finish_frame(GdkFrameClock *clock, FrameRecord *record) {
	/* Watching starts partway through a frame, so its start is missing */
	if (!record->paint_start)
		return;
	gint64 work = g_get_monotonic_time() - record->paint_start;
	record->frames++;
	record->work_time += work;
	record->heaviest_work = MAX(record->heaviest_work, work);
	if (work > HEAVY_WORK_MICROSECONDS)
		record->heavy_frames++;
	if (record->input_time)
		note_response(record);
	if (record->drawing_time >= SUMMARY_MICROSECONDS)
		write_summary(record);
}


/* A late check means the main thread was busy, which freezes the window however few frames it was asked for */
static gboolean check_heartbeat(gpointer data) {
	gint64 *last_beat = data;
	gint64 now = g_get_monotonic_time();
	gint64 late = now - *last_beat - HEARTBEAT_MILLISECONDS * G_TIME_SPAN_MILLISECOND;
	if (late > HELD_MICROSECONDS)
		g_debug("Window held up for %.0f ms", to_milliseconds(late));
	*last_beat = now;
	return G_SOURCE_CONTINUE;
}


static void stop_heartbeat(GtkWidget *window, gpointer heartbeat) {
	g_source_remove(GPOINTER_TO_UINT(heartbeat));
}


/* Gets each click and key before the widgets that handle them, and requests a frame so a click that changes no
 * pixels is timed too */
static gboolean note_input(GtkEventControllerLegacy *controller, GdkEvent *event, FrameRecord *record) {
	GdkEventType type = gdk_event_get_event_type(event);
	if ((type != GDK_BUTTON_PRESS && type != GDK_KEY_PRESS) || record->input_time)
		return GDK_EVENT_PROPAGATE;
	record->input_time = g_get_monotonic_time();
	GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	gdk_frame_clock_request_phase(gtk_widget_get_frame_clock(window), GDK_FRAME_CLOCK_PHASE_PAINT);
	return GDK_EVENT_PROPAGATE;
}


void watch_frame_rate(GtkWidget *window) {
	GdkFrameClock *clock = gtk_widget_get_frame_clock(window);
	g_return_if_fail(clock);
	FrameRecord *record = g_new0(FrameRecord, 1);
	g_object_set_data_full(G_OBJECT(clock), "frame-record", record, g_free);
	g_signal_connect(clock, "before-paint", G_CALLBACK(start_frame), record);
	g_signal_connect(clock, "after-paint", G_CALLBACK(finish_frame), record);
	GtkEventController *input = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(input, GTK_PHASE_CAPTURE);
	g_signal_connect(input, "event", G_CALLBACK(note_input), record);
	gtk_widget_add_controller(window, input);
	gint64 *last_beat = g_new(gint64, 1);
	*last_beat = g_get_monotonic_time();
	guint heartbeat = g_timeout_add_full(G_PRIORITY_HIGH, HEARTBEAT_MILLISECONDS, check_heartbeat, last_beat, g_free);
	g_signal_connect(window, "destroy", G_CALLBACK(stop_heartbeat), GUINT_TO_POINTER(heartbeat));
}
