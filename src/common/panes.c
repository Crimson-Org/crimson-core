#include <math.h>

#include "panes.h"

/* The same spring a libadwaita page view slides with */
#define SLIDE_DAMPING 1.0
#define SLIDE_MASS 1.0
#define SLIDE_STIFFNESS 1000.0

enum {
	PROPERTY_COLLAPSED = 1,
	PROPERTY_SHOW_CONTENT,
	PROPERTY_COUNT,
};

struct _CrimsonPanes {
	GtkWidget parent_instance;
	GtkWidget *sidebar, *content;
	gboolean is_collapsed, shows_content;
	int sidebar_width;
	/* How far collapsed panes have slid toward the content, from 0 on the side panel to 1 on the content */
	double progress;
	AdwAnimation *slide;
	AdwSwipeTracker *swipe;
};

static GParamSpec *properties[PROPERTY_COUNT];

static void make_swipeable(AdwSwipeableInterface *interface);

G_DEFINE_FINAL_TYPE_WITH_CODE(CrimsonPanes, crimson_panes, GTK_TYPE_WIDGET,
	G_IMPLEMENT_INTERFACE(ADW_TYPE_SWIPEABLE, make_swipeable))


/* A pane that is wholly out of sight is neither drawn nor laid out, while staying in place with all its rows */
static void show_panes(CrimsonPanes *panes) {
	gtk_widget_set_child_visible(panes->sidebar, !panes->is_collapsed || panes->progress < 1);
	gtk_widget_set_child_visible(panes->content, !panes->is_collapsed || panes->progress > 0);
	gtk_widget_queue_allocate(GTK_WIDGET(panes));
}


static void set_progress(double progress, gpointer data) {
	CrimsonPanes *panes = data;
	panes->progress = progress;
	show_panes(panes);
}


/* Only the shown content can be swiped back to the side panel, as a page view's pages are */
static void update_swipe(CrimsonPanes *panes) {
	adw_swipe_tracker_set_enabled(panes->swipe, panes->is_collapsed && panes->shows_content);
}


static void slide_to(CrimsonPanes *panes, double target, double velocity) {
	AdwSpringAnimation *spring = ADW_SPRING_ANIMATION(panes->slide);
	adw_spring_animation_set_value_from(spring, panes->progress);
	adw_spring_animation_set_value_to(spring, target);
	adw_spring_animation_set_initial_velocity(spring, velocity);
	adw_animation_play(panes->slide);
}


static void start_swipe(AdwSwipeTracker *swipe, CrimsonPanes *panes) {
	adw_animation_pause(panes->slide);
}


static void follow_swipe(AdwSwipeTracker *swipe, double progress, CrimsonPanes *panes) {
	set_progress(progress, panes);
}


static void end_swipe(AdwSwipeTracker *swipe, double velocity, double target, CrimsonPanes *panes) {
	slide_to(panes, target, velocity);
	gboolean shows_content = target > 0.5;
	if (shows_content == panes->shows_content)
		return;
	panes->shows_content = shows_content;
	update_swipe(panes);
	g_object_notify_by_pspec(G_OBJECT(panes), properties[PROPERTY_SHOW_CONTENT]);
}


static double get_swipe_distance(AdwSwipeable *swipeable) {
	return gtk_widget_get_width(GTK_WIDGET(swipeable));
}


static double *get_snap_points(AdwSwipeable *swipeable, int *count) {
	static const double POINTS[] = {0, 1};
	*count = G_N_ELEMENTS(POINTS);
	return g_memdup2(POINTS, sizeof(POINTS));
}


static double get_swipe_progress(AdwSwipeable *swipeable) {
	return CRIMSON_PANES(swipeable)->progress;
}


static double get_cancel_progress(AdwSwipeable *swipeable) {
	return CRIMSON_PANES(swipeable)->shows_content ? 1 : 0;
}


static void make_swipeable(AdwSwipeableInterface *interface) {
	interface->get_distance = get_swipe_distance;
	interface->get_snap_points = get_snap_points;
	interface->get_progress = get_swipe_progress;
	interface->get_cancel_progress = get_cancel_progress;
}


static void measure_panes(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural,
	int *minimum_baseline, int *natural_baseline) {
	CrimsonPanes *panes = CRIMSON_PANES(widget);
	int sidebar_minimum, sidebar_natural, content_minimum, content_natural;
	gtk_widget_measure(panes->sidebar, orientation, -1, &sidebar_minimum, &sidebar_natural, NULL, NULL);
	gtk_widget_measure(panes->content, orientation, -1, &content_minimum, &content_natural, NULL, NULL);
	if (orientation == GTK_ORIENTATION_HORIZONTAL && !panes->is_collapsed) {
		int sidebar_width = MAX(sidebar_minimum, panes->sidebar_width);
		*minimum = sidebar_width + content_minimum;
		*natural = sidebar_width + content_natural;
		return;
	}
	*minimum = MAX(sidebar_minimum, content_minimum);
	*natural = MAX(sidebar_natural, content_natural);
}


static void place_pane(GtkWidget *pane, int x, int pane_width, int width, int height) {
	if (!gtk_widget_get_child_visible(pane))
		return;
	if (gtk_widget_get_direction(pane) == GTK_TEXT_DIR_RTL)
		x = width - x - pane_width;
	gtk_widget_size_allocate(pane, &(GtkAllocation){x, 0, pane_width, height}, -1);
}


/* Collapsed panes slide side by side like pages, each as wide as the panes */
static void allocate_panes(GtkWidget *widget, int width, int height, int baseline) {
	CrimsonPanes *panes = CRIMSON_PANES(widget);
	if (panes->is_collapsed) {
		int offset = (int)round(width * panes->progress);
		place_pane(panes->sidebar, -offset, width, width, height);
		place_pane(panes->content, width - offset, width, width, height);
		return;
	}
	int sidebar_minimum;
	gtk_widget_measure(panes->sidebar, GTK_ORIENTATION_HORIZONTAL, -1, &sidebar_minimum, NULL, NULL, NULL);
	int sidebar_width = MIN(MAX(sidebar_minimum, panes->sidebar_width), width);
	place_pane(panes->sidebar, 0, sidebar_width, width, height);
	place_pane(panes->content, sidebar_width, width - sidebar_width, width, height);
}


static void follow_direction(GtkWidget *widget, GtkTextDirection previous) {
	CrimsonPanes *panes = CRIMSON_PANES(widget);
	adw_swipe_tracker_set_reversed(panes->swipe, gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL);
	gtk_widget_queue_allocate(widget);
}


static void get_property(GObject *object, guint id, GValue *value, GParamSpec *spec) {
	CrimsonPanes *panes = CRIMSON_PANES(object);
	switch (id) {
	case PROPERTY_COLLAPSED: g_value_set_boolean(value, panes->is_collapsed); break;
	case PROPERTY_SHOW_CONTENT: g_value_set_boolean(value, panes->shows_content); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
	}
}


static void set_property(GObject *object, guint id, const GValue *value, GParamSpec *spec) {
	CrimsonPanes *panes = CRIMSON_PANES(object);
	switch (id) {
	case PROPERTY_COLLAPSED: crimson_panes_set_collapsed(panes, g_value_get_boolean(value)); break;
	case PROPERTY_SHOW_CONTENT: crimson_panes_set_show_content(panes, g_value_get_boolean(value)); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
	}
}


static void dispose_panes(GObject *object) {
	CrimsonPanes *panes = CRIMSON_PANES(object);
	g_clear_object(&panes->slide);
	g_clear_object(&panes->swipe);
	g_clear_pointer(&panes->sidebar, gtk_widget_unparent);
	g_clear_pointer(&panes->content, gtk_widget_unparent);
	G_OBJECT_CLASS(crimson_panes_parent_class)->dispose(object);
}


static void crimson_panes_class_init(CrimsonPanesClass *class) {
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose_panes;
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
	widget_class->measure = measure_panes;
	widget_class->size_allocate = allocate_panes;
	widget_class->direction_changed = follow_direction;
	properties[PROPERTY_COLLAPSED] = g_param_spec_boolean("collapsed", NULL, NULL, FALSE,
		G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	properties[PROPERTY_SHOW_CONTENT] = g_param_spec_boolean("show-content", NULL, NULL, FALSE,
		G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties(object_class, PROPERTY_COUNT, properties);
}


static void crimson_panes_init(CrimsonPanes *panes) {
	gtk_widget_set_overflow(GTK_WIDGET(panes), GTK_OVERFLOW_HIDDEN);
	AdwAnimationTarget *target = adw_callback_animation_target_new(set_progress, panes, NULL);
	AdwSpringParams *spring = adw_spring_params_new(SLIDE_DAMPING, SLIDE_MASS, SLIDE_STIFFNESS);
	panes->slide = adw_spring_animation_new(GTK_WIDGET(panes), 0, 0, spring, target);
	adw_spring_animation_set_clamp(ADW_SPRING_ANIMATION(panes->slide), TRUE);
	panes->swipe = adw_swipe_tracker_new(ADW_SWIPEABLE(panes));
	g_signal_connect(panes->swipe, "begin-swipe", G_CALLBACK(start_swipe), panes);
	g_signal_connect(panes->swipe, "update-swipe", G_CALLBACK(follow_swipe), panes);
	g_signal_connect(panes->swipe, "end-swipe", G_CALLBACK(end_swipe), panes);
	update_swipe(panes);
}


/* Docked panes carry libadwaita's pane styles, which give the side panel its colour and edge */
static void style_panes(CrimsonPanes *panes) {
	if (panes->is_collapsed) {
		gtk_widget_remove_css_class(panes->sidebar, "sidebar-pane");
		gtk_widget_remove_css_class(panes->content, "content-pane");
		return;
	}
	gtk_widget_add_css_class(panes->sidebar, "sidebar-pane");
	gtk_widget_add_css_class(panes->content, "content-pane");
}


GtkWidget *crimson_panes_new(GtkWidget *sidebar, GtkWidget *content) {
	CrimsonPanes *panes = g_object_new(CRIMSON_TYPE_PANES, NULL);
	panes->sidebar = sidebar;
	panes->content = content;
	gtk_widget_set_parent(sidebar, GTK_WIDGET(panes));
	gtk_widget_set_parent(content, GTK_WIDGET(panes));
	style_panes(panes);
	show_panes(panes);
	return GTK_WIDGET(panes);
}


gboolean crimson_panes_get_collapsed(CrimsonPanes *panes) {
	return panes->is_collapsed;
}


void crimson_panes_set_collapsed(CrimsonPanes *panes, gboolean is_collapsed) {
	is_collapsed = !!is_collapsed;
	if (panes->is_collapsed == is_collapsed)
		return;
	panes->is_collapsed = is_collapsed;
	adw_animation_skip(panes->slide);
	panes->progress = panes->shows_content ? 1 : 0;
	style_panes(panes);
	update_swipe(panes);
	show_panes(panes);
	gtk_widget_queue_resize(GTK_WIDGET(panes));
	g_object_notify_by_pspec(G_OBJECT(panes), properties[PROPERTY_COLLAPSED]);
}


gboolean crimson_panes_get_show_content(CrimsonPanes *panes) {
	return panes->shows_content;
}


void crimson_panes_set_show_content(CrimsonPanes *panes, gboolean shows_content) {
	shows_content = !!shows_content;
	if (panes->shows_content == shows_content)
		return;
	panes->shows_content = shows_content;
	update_swipe(panes);
	if (panes->is_collapsed && gtk_widget_get_mapped(GTK_WIDGET(panes)))
		slide_to(panes, shows_content ? 1 : 0, 0);
	else
		set_progress(shows_content ? 1 : 0, panes);
	g_object_notify_by_pspec(G_OBJECT(panes), properties[PROPERTY_SHOW_CONTENT]);
}


void crimson_panes_set_sidebar_width(CrimsonPanes *panes, int width) {
	panes->sidebar_width = width;
	gtk_widget_queue_resize(GTK_WIDGET(panes));
}
