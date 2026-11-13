#pragma once

#include <adwaita.h>

/* A press anywhere outside the dialog's own box closes it, as clicking away from a popover does */
void close_on_outside_click(AdwDialog *dialog);

/* Gives a dialog a height that leaves its window in sight around it, and grows with a taller window up to most, so it
 * never nearly fills a small window nor wastes a large one */
void fit_dialog_height(AdwDialog *dialog, GtkWidget *parent, int most);
