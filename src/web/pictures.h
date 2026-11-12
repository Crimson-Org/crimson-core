#pragma once

#include <adwaita.h>

/* Loads a picture from the web once and shares it among every GtkPicture or AdwAvatar showing its address, with
 * kept pictures saved on disk so they show at once on the next start and are only fetched again when changed */
void show_web_picture(GtkWidget *widget, const char *address, gboolean is_kept);
/* The picture at the address once it has loaded, or NULL while it loads or after it failed */
GdkTexture *get_web_picture(const char *address);
typedef void (*PictureReady)(GObject *owner);
/* Loads a picture and calls back with its owner once it is ready, unless the owner is gone by then */
void preload_web_picture(const char *address, PictureReady ready, GObject *owner);
/* Pictures are decoded no larger than this on their longest side, which suits the largest place the program
 * shows one */
void set_web_picture_size_limit(int pixels);
/* Asked whether to accept a certificate the system does not trust, such as one the user trusted in the program */
typedef gboolean (*CertificateCheck)(GTlsCertificate *certificate);
void set_web_picture_certificate_check(CertificateCheck check);
