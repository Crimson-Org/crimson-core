#include <math.h>

#include <adwaita.h>

#include "smooth-scroll.h"

#define WHEEL_DURATION 240
/* GTK scrolls this power of the page size for one wheel step */
#define WHEEL_STEP_POWER (2.0 / 3.0)
#define VERTICAL_GLIDE_KEY "vertical-glide"
#define HORIZONTAL_GLIDE_KEY "horizontal-glide"

/* The one running glide along one direction of a scroller and where it is heading */
typedef struct {
	GtkAdjustment *adjustment;
	AdwAnimation *animation;
	double goal;
} Glide;


static void free_glide(gpointer data) {
	Glide *glide = data;
	if (glide->animation)
		adw_animation_pause(glide->animation);
	g_clear_object(&glide->animation);
	g_free(glide);
}


static void set_scroll_value(double value, gpointer adjustment) {
	gtk_adjustment_set_value(adjustment, value);
}


static Glide *get_glide(GtkScrolledWindow *scroller, gboolean is_horizontal) {
	const char *key = is_horizontal ? HORIZONTAL_GLIDE_KEY : VERTICAL_GLIDE_KEY;
	Glide *glide = g_object_get_data(G_OBJECT(scroller), key);
	if (glide)
		return glide;
	glide = g_new0(Glide, 1);
	glide->adjustment = is_horizontal ? gtk_scrolled_window_get_hadjustment(scroller)
		: gtk_scrolled_window_get_vadjustment(scroller);
	AdwAnimationTarget *target = adw_callback_animation_target_new(set_scroll_value, glide->adjustment, NULL);
	glide->animation = adw_timed_animation_new(GTK_WIDGET(scroller), 0, 0, WHEEL_DURATION, target);
	adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(glide->animation), ADW_EASE_OUT_CUBIC);
	g_object_set_data_full(G_OBJECT(scroller), key, glide, free_glide);
	return glide;
}


static gboolean is_gliding(Glide *glide) {
	return adw_animation_get_state(glide->animation) == ADW_ANIMATION_PLAYING;
}


static void glide_to(Glide *glide, double goal, guint duration) {
	GtkAdjustment *adjustment = glide->adjustment;
	double maximum = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment);
	glide->goal = CLAMP(goal, gtk_adjustment_get_lower(adjustment), MAX(maximum, 0));
	adw_animation_pause(glide->animation);
	adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(glide->animation), gtk_adjustment_get_value(adjustment));
	adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(glide->animation), glide->goal);
	adw_timed_animation_set_duration(ADW_TIMED_ANIMATION(glide->animation), duration);
	adw_animation_play(glide->animation);
}


void glide_scroller(GtkScrolledWindow *scroller, double goal, guint duration) {
	glide_to(get_glide(scroller, FALSE), goal, duration);
}


static gboolean can_scroll(GtkScrolledWindow *scroller, gboolean is_horizontal) {
	GtkPolicyType horizontal, vertical;
	gtk_scrolled_window_get_policy(scroller, &horizontal, &vertical);
	GtkPolicyType policy = is_horizontal ? horizontal : vertical;
	GtkAdjustment *adjustment = is_horizontal ? gtk_scrolled_window_get_hadjustment(scroller)
		: gtk_scrolled_window_get_vadjustment(scroller);
	double room = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment)
		- gtk_adjustment_get_lower(adjustment);
	return policy != GTK_POLICY_NEVER && policy != GTK_POLICY_EXTERNAL && room > 0;
}


/* Shift or a sideways wheel scrolls across, and a scroller that cannot move that way leaves the step to the
 * scroller inside or around it */
static gboolean scroll_wheel(GtkEventControllerScroll *controller, double delta_x, double delta_y,
	GtkScrolledWindow *scroller) {
	GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
	if (!event || gdk_scroll_event_get_unit(event) != GDK_SCROLL_UNIT_WHEEL)
		return GDK_EVENT_PROPAGATE;
	GdkModifierType modifiers = gdk_event_get_modifier_state(event);
	if (modifiers & GDK_CONTROL_MASK)
		return GDK_EVENT_PROPAGATE;
	gboolean is_horizontal = delta_x != 0 || (modifiers & GDK_SHIFT_MASK);
	double delta = delta_x != 0 ? delta_x : delta_y;
	if (delta == 0 || !can_scroll(scroller, is_horizontal))
		return GDK_EVENT_PROPAGATE;
	Glide *glide = get_glide(scroller, is_horizontal);
	double start = is_gliding(glide) ? glide->goal : gtk_adjustment_get_value(glide->adjustment);
	double step = pow(gtk_adjustment_get_page_size(glide->adjustment), WHEEL_STEP_POWER);
	glide_to(glide, start + delta * step, WHEEL_DURATION);
	return GDK_EVENT_STOP;
}


/* Catching the step before the scroller's own handling keeps GTK from jumping first, and an outer scroller that
 * cannot move along the step leaves it for the one inside */
void add_smooth_scrolling(GtkScrolledWindow *scroller) {
	GtkEventController *controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
	g_signal_connect(controller, "scroll", G_CALLBACK(scroll_wheel), scroller);
	gtk_widget_add_controller(GTK_WIDGET(scroller), controller);
}


void set_scroll_child(GtkScrolledWindow *scroller, GtkWidget *child) {
	gtk_scrolled_window_set_child(scroller, child);
	GtkWidget *viewport = gtk_scrolled_window_get_child(scroller);
	if (GTK_IS_VIEWPORT(viewport))
		gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(viewport), FALSE);
}
