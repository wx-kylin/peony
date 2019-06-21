/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/*
 * Peony
 *
 * Copyright (C) 2000 Eazel, Inc.
 *
 * Peony is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Peony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: Darin Adler <darin@bentspoon.com>
 */

#include <config.h>
#include "peony-desktop-window.h"
#include "peony-window-private.h"
#include "peony-actions.h"

#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <eel/eel-vfs-extensions.h>
#include <eel/eel-background.h>
#include <libpeony-private/peony-file-utilities.h>
#include <libpeony-private/peony-icon-names.h>
#include <gio/gio.h>
#include <glib/gi18n.h>    

#include <libpeony-private/peony-global-preferences.h>

struct PeonyDesktopWindowDetails
{
    gulong size_changed_id;

    gboolean loaded;
};

G_DEFINE_TYPE (PeonyDesktopWindow, peony_desktop_window,
               PEONY_TYPE_SPATIAL_WINDOW);


static void peony_preferences_desktop_is_home_changed_cb
(PeonyWindow *window)
{
    g_return_if_fail (PEONY_IS_WINDOW (window));

    peony_window_reload (window);
}

static void
peony_desktop_window_init (PeonyDesktopWindow *window)
{
	    GtkAction *action;
	        AtkObject *accessible;

		    window->details = G_TYPE_INSTANCE_GET_PRIVATE (window, PEONY_TYPE_DESKTOP_WINDOW,
				                                                       PeonyDesktopWindowDetails);

		        GtkStyleContext *context;

			    context = gtk_widget_get_style_context (GTK_WIDGET (window));
			        gtk_style_context_add_class (context, "peony-desktop-window");

				    gtk_window_move (GTK_WINDOW (window), 0, 0);

				        /* shouldn't really be needed given our semantic type
					 *      * of _NET_WM_TYPE_DESKTOP, but why not
					 *           */
				        gtk_window_set_resizable (GTK_WINDOW (window),
							                              FALSE);

					    g_object_set_data (G_OBJECT (window), "is_desktop_window",
							                           GINT_TO_POINTER (1));

					        gtk_widget_hide (PEONY_WINDOW (window)->details->statusbar);
						    gtk_widget_hide (PEONY_WINDOW (window)->details->menubar);
						    /* Don't allow close action on desktop */
						        action = gtk_action_group_get_action (PEONY_WINDOW (window)->details->main_action_group,
									                                          PEONY_ACTION_CLOSE);
							    gtk_action_set_sensitive (action, FALSE);

							        /* Set the accessible name so that it doesn't inherit the cryptic desktop URI. */
							        accessible = gtk_widget_get_accessible (GTK_WIDGET (window));

								    if (accessible) {
									            atk_object_set_name (accessible, _("Desktop"));
										        }

    peony_global_preferences_init ();
    g_signal_connect_swapped (peony_preferences, "changed::desktop-is-home-dir", G_CALLBACK (peony_preferences_desktop_is_home_changed_cb), window);
}

static gint
peony_desktop_window_delete_event (PeonyDesktopWindow *window)
{
    /* Returning true tells GTK+ not to delete the window. */
    return TRUE;
}

void
peony_desktop_window_update_directory (PeonyDesktopWindow *window)
{
    GFile *location;

    g_assert (PEONY_IS_DESKTOP_WINDOW (window));

    location = g_file_new_for_uri (EEL_DESKTOP_URI);
    peony_window_go_to (PEONY_WINDOW (window), location);
    window->details->loaded = TRUE;

    g_object_unref (location);
}

static void
peony_desktop_window_screen_size_changed (GdkScreen             *screen,
        PeonyDesktopWindow *window)
{
    int width_request, height_request;

    width_request = gdk_screen_get_width (screen);
    height_request = gdk_screen_get_height (screen);

    g_object_set (window,
                  "width_request", width_request,
                  "height_request", height_request,
                  NULL);
}

PeonyDesktopWindow *
peony_desktop_window_new (PeonyApplication *application,
                         GdkScreen           *screen)
{
    PeonyDesktopWindow *window;
    int width_request, height_request;

    width_request = gdk_screen_get_width (screen);
    height_request = gdk_screen_get_height (screen);

    window = PEONY_DESKTOP_WINDOW
             (gtk_widget_new (peony_desktop_window_get_type(),
                              "app", application,
                              "width_request", width_request,
                              "height_request", height_request,
                              "screen", screen,
                              NULL));
    /* Stop wrong desktop window size in GTK 3.20*/
    /* We don't want to set a default size, which the parent does, since this */
    /* will cause the desktop window to open at the wrong size in gtk 3.20 */
#if GTK_CHECK_VERSION (3, 20, 0)
    gtk_window_set_default_size (GTK_WINDOW (window), -1, -1);
#endif
    /* Special sawmill setting*/
    gtk_window_set_wmclass (GTK_WINDOW (window), "desktop_window", "Peony");

    g_signal_connect (window, "delete_event", G_CALLBACK (peony_desktop_window_delete_event), NULL);

    /* Point window at the desktop folder.
     * Note that peony_desktop_window_init is too early to do this.
     */
    peony_desktop_window_update_directory (window);

    return window;
}

static void
map (GtkWidget *widget)
{
    /* Chain up to realize our children */
    GTK_WIDGET_CLASS (peony_desktop_window_parent_class)->map (widget);
    gdk_window_lower (gtk_widget_get_window (widget));
}

static void
unrealize (GtkWidget *widget)
{
    PeonyDesktopWindow *window;
    PeonyDesktopWindowDetails *details;
    GdkWindow *root_window;

    window = PEONY_DESKTOP_WINDOW (widget);
    details = window->details;

    root_window = gdk_screen_get_root_window (
                      gtk_window_get_screen (GTK_WINDOW (window)));

    gdk_property_delete (root_window,
                         gdk_atom_intern ("PEONY_DESKTOP_WINDOW_ID", TRUE));

    if (details->size_changed_id != 0) {
        g_signal_handler_disconnect (gtk_window_get_screen (GTK_WINDOW (window)),
                         details->size_changed_id);
        details->size_changed_id = 0;
    }

    GTK_WIDGET_CLASS (peony_desktop_window_parent_class)->unrealize (widget);
}

static void
set_wmspec_desktop_hint (GdkWindow *window)
{
    GdkAtom atom;

    atom = gdk_atom_intern ("_NET_WM_WINDOW_TYPE_DESKTOP", FALSE);

    gdk_property_change (window,
                         gdk_atom_intern ("_NET_WM_WINDOW_TYPE", FALSE),
                         gdk_x11_xatom_to_atom (XA_ATOM), 32,
                         GDK_PROP_MODE_REPLACE, (guchar *) &atom, 1);
}

static void
set_desktop_window_id (PeonyDesktopWindow *window,
                       GdkWindow             *gdkwindow)
{
    /* Tuck the desktop windows xid in the root to indicate we own the desktop.
     */
    Window window_xid;
    GdkWindow *root_window;

    root_window = gdk_screen_get_root_window (
                      gtk_window_get_screen (GTK_WINDOW (window)));

    window_xid = GDK_WINDOW_XID (gdkwindow);

    gdk_property_change (root_window,
                         gdk_atom_intern ("PEONY_DESKTOP_WINDOW_ID", FALSE),
                         gdk_x11_xatom_to_atom (XA_WINDOW), 32,
                         GDK_PROP_MODE_REPLACE, (guchar *) &window_xid, 1);
}

static void
realize (GtkWidget *widget)
{
    PeonyDesktopWindow *window;
    PeonyDesktopWindowDetails *details;
    window = PEONY_DESKTOP_WINDOW (widget);
    details = window->details;

    /* Make sure we get keyboard events */
    gtk_widget_set_events (widget, gtk_widget_get_events (widget)
                           | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
    /* Do the work of realizing. */
    GTK_WIDGET_CLASS (peony_desktop_window_parent_class)->realize (widget);

    /* This is the new way to set up the desktop window */
    set_wmspec_desktop_hint (gtk_widget_get_window (widget));

    set_desktop_window_id (window, gtk_widget_get_window (widget));

    details->size_changed_id =
        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (window)), "size_changed",
                          G_CALLBACK (peony_desktop_window_screen_size_changed), window);
}

#if GTK_CHECK_VERSION (3, 22, 0)
static gboolean
draw (GtkWidget *widget,
      cairo_t   *cr)
{
    eel_background_draw (widget, cr);

    return GTK_WIDGET_CLASS (peony_desktop_window_parent_class)->draw (widget, cr);
}
#endif

static char *
real_get_title (PeonyWindow *window)
{
    return g_strdup (_("Desktop"));
}

static PeonyIconInfo *
real_get_icon (PeonyWindow *window,
               PeonyWindowSlot *slot)
{
    return peony_icon_info_lookup_from_name (PEONY_ICON_DESKTOP, 48);
}

static void
peony_desktop_window_class_init (PeonyDesktopWindowClass *klass)
{
    GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);
    PeonyWindowClass *nclass = PEONY_WINDOW_CLASS (klass);

    wclass->realize = realize;
    wclass->unrealize = unrealize;
    wclass->map = map;
#if GTK_CHECK_VERSION (3, 22, 0)
    wclass->draw = draw;
#endif
    nclass->window_type = PEONY_WINDOW_DESKTOP;
    nclass->get_title = real_get_title;
    nclass->get_icon = real_get_icon;

    g_type_class_add_private (klass, sizeof (PeonyDesktopWindowDetails));
}

gboolean
peony_desktop_window_loaded (PeonyDesktopWindow *window)
{
    return window->details->loaded;
}
