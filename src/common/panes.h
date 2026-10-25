#pragma once

#include <adwaita.h>

/* A side panel and a main pane that sit side by side, or become two pages a narrow window slides between
 * They never move to another parent, since GTK lists throw away and rebuild every row whenever they do */
#define CRIMSON_TYPE_PANES (crimson_panes_get_type())
G_DECLARE_FINAL_TYPE(CrimsonPanes, crimson_panes, CRIMSON, PANES, GtkWidget)

GtkWidget *crimson_panes_new(GtkWidget *sidebar, GtkWidget *content);
gboolean crimson_panes_get_collapsed(CrimsonPanes *panes);
/* Collapsed panes fill the whole width one at a time, which a breakpoint sets through the "collapsed" property */
void crimson_panes_set_collapsed(CrimsonPanes *panes, gboolean is_collapsed);
gboolean crimson_panes_get_show_content(CrimsonPanes *panes);
/* Picks the page collapsed panes show, sliding to it, and is kept for when they collapse again */
void crimson_panes_set_show_content(CrimsonPanes *panes, gboolean shows_content);
void crimson_panes_set_sidebar_width(CrimsonPanes *panes, int width);
