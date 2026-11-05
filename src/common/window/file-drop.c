#include "common/window/file-drop.h"

/* What a drop target calls when files land on it */
typedef struct {
	FilesDropped dropped;
	gpointer data;
} FileDrop;


static gboolean drop_files(GtkDropTarget *target, const GValue *value, double x, double y, FileDrop *drop) {
	GSList *files = gdk_file_list_get_files(g_value_get_boxed(value));
	g_autoptr(GPtrArray) list = g_ptr_array_new();
	for (GSList *item = files; item; item = item->next)
		g_ptr_array_add(list, item->data);
	g_slist_free(files);
	if (list->len)
		drop->dropped((GFile **)list->pdata, list->len, drop->data);
	return list->len > 0;
}


void add_file_drop(GtkWidget *widget, FilesDropped dropped, gpointer data) {
	FileDrop *drop = g_new0(FileDrop, 1);
	*drop = (FileDrop){dropped, data};
	GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
	g_object_set_data_full(G_OBJECT(target), "file-drop", drop, g_free);
	g_signal_connect(target, "drop", G_CALLBACK(drop_files), drop);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(target));
}
