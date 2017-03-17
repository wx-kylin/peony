/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
   peony-directory-background.c: Helper for the background of a widget
                                that is viewing a particular location.

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2012 Jasmine Hassan <jasmine.aura@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Authors: Darin Adler <darin@bentspoon.com>
            Jasmine Hassan <jasmine.aura@gmail.com>
*/

#include <config.h>
#include "peony-directory-background.h"

#include <eel/eel-gdk-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-background.h>
#include "peony-dnd.h"
#include "peony-global-preferences.h"
#include "peony-metadata.h"
#include "peony-file-attributes.h"
#include <gtk/gtk.h>
#include <string.h>

static void peony_background_changed_cb (EelBackground *background,
                                        GdkDragAction  action,
                                        PeonyFile      *file);

static void
peony_background_get_default_settings (char **color,
                                      char **image)
{
    gboolean background_set;

    background_set = g_settings_get_boolean (peony_preferences, PEONY_PREFERENCES_BACKGROUND_SET);

    if (background_set && color)
        *color = g_settings_get_string (peony_preferences, PEONY_PREFERENCES_BACKGROUND_COLOR);

    if (background_set && image)
        *image =  g_settings_get_string (peony_preferences, PEONY_PREFERENCES_BACKGROUND_URI);
}

static void
peony_background_load_from_file_metadata (PeonyFile      *file,
                                         EelBackground *background)
{
    char *color, *image;

    g_assert (EEL_IS_BACKGROUND (background));
    g_assert (PEONY_IS_FILE (file));
    g_assert (g_object_get_data (G_OBJECT (background), "eel_background_file") == file);

    color = peony_file_get_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_COLOR, NULL);
    image = peony_file_get_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_IMAGE, NULL);

    /* if there's none, read the default from the theme */
    if (color == NULL && image == NULL)
        peony_background_get_default_settings (&color, &image);

    /* Block the other handler while we are responding to changes
     * in the metadata so it doesn't try to change the metadata.
     */
    g_signal_handlers_block_by_func (background, G_CALLBACK (peony_background_changed_cb), file);

    eel_background_set_color (background, color);
    /* non-tiled only available for desktop, at least for now */
    eel_bg_set_placement (background, MATE_BG_PLACEMENT_TILED);
    eel_background_set_image_uri (background, image);

    /* Unblock the handler. */
    g_signal_handlers_unblock_by_func (background, G_CALLBACK (peony_background_changed_cb), file);

    g_free (color);
    g_free (image);
}

/* handle the file changed signal */
static void
peony_background_settings_notify_cb (PeonyFile *file,
                                    EelBackground *background)
{
    peony_background_load_from_file_metadata (file, background);
}

/* handle the theme changing */
static void
peony_background_theme_notify_cb (GSettings   *settings,
                                 const gchar *key,
                                 gpointer     user_data)
{
    PeonyFile *file;
    EelBackground *background = EEL_BACKGROUND (user_data);

    file = g_object_get_data (G_OBJECT (background), "eel_background_file");

    if (file)
        peony_background_settings_notify_cb (file, background);
}

/* handle the background changed signal */
static void
peony_background_changed_cb (EelBackground *background,
                            GdkDragAction  action,
                            PeonyFile   *file)
{
    g_assert (EEL_IS_BACKGROUND (background));
    g_assert (PEONY_IS_FILE (file));
    g_assert (g_object_get_data (G_OBJECT (background), "eel_background_file") == file);

    char *color = eel_background_get_color (background);
    char *image = eel_background_get_image_uri (background);

    /* Block the other handler while we are writing metadata so it doesn't
     * try to change the background.
     */
    g_signal_handlers_block_by_func (file, G_CALLBACK (peony_background_settings_notify_cb),
                                     background);

    if (action != (GdkDragAction) PEONY_DND_ACTION_SET_AS_FOLDER_BACKGROUND &&
            action != (GdkDragAction) PEONY_DND_ACTION_SET_AS_GLOBAL_BACKGROUND)
    {
        action = (GdkDragAction) GPOINTER_TO_INT (g_object_get_data (G_OBJECT (background),
                                                  "default_drag_action"));
    }

    if (action == (GdkDragAction) PEONY_DND_ACTION_SET_AS_GLOBAL_BACKGROUND)
    {
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_COLOR, NULL, NULL);
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_IMAGE, NULL, NULL);

        g_signal_handlers_block_by_func (peony_preferences,
                                         G_CALLBACK (peony_background_theme_notify_cb),
                                         background);

        g_settings_set_string (peony_preferences,
                               PEONY_PREFERENCES_BACKGROUND_COLOR, color ? color : "");
        g_settings_set_string (peony_preferences,
                               PEONY_PREFERENCES_BACKGROUND_URI, image ? image : "");

        g_settings_set_boolean (peony_preferences, PEONY_PREFERENCES_BACKGROUND_SET, TRUE);

        g_signal_handlers_unblock_by_func (peony_preferences,
                                           G_CALLBACK (peony_background_theme_notify_cb),
                                           background);
    } else {
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_COLOR, NULL, color);
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_IMAGE, NULL, image);
    }

    /* Unblock the handler. */
    g_signal_handlers_unblock_by_func (file, G_CALLBACK (peony_background_settings_notify_cb),
                                       background);

    g_free (color);
    g_free (image);
}

/* handle the background reset signal by setting values from the current theme */
static void
peony_background_reset_cb (EelBackground *background,
                          PeonyFile  *file)
{
    char *color, *image;

    /* Block the other handler while we are writing metadata so it doesn't
     * try to change the background.
     */
    g_signal_handlers_block_by_func (file, G_CALLBACK (peony_background_settings_notify_cb),
                                     background);

    color = peony_file_get_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_COLOR, NULL);
    image = peony_file_get_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_IMAGE, NULL);
    if (!color && !image)
    {
        g_signal_handlers_block_by_func (peony_preferences,
                                         G_CALLBACK (peony_background_theme_notify_cb),
                                         background);
        g_settings_set_boolean (peony_preferences, PEONY_PREFERENCES_BACKGROUND_SET, FALSE);
        g_signal_handlers_unblock_by_func (peony_preferences,
                                           G_CALLBACK (peony_background_theme_notify_cb),
                                           background);
    }
    else
    {
        /* reset the metadata */
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_COLOR, NULL, NULL);
        peony_file_set_metadata (file, PEONY_METADATA_KEY_LOCATION_BACKGROUND_IMAGE, NULL, NULL);
    }
    g_free (color);
    g_free (image);

    /* Unblock the handler. */
    g_signal_handlers_unblock_by_func (file, G_CALLBACK (peony_background_settings_notify_cb),
                                       background);

    peony_background_settings_notify_cb (file, background);
}

/* handle the background destroyed signal */
static void
peony_background_weak_notify (gpointer data,
                             GObject *background)
{
    PeonyFile *file = PEONY_FILE (data);

    g_signal_handlers_disconnect_by_func (file, G_CALLBACK (peony_background_settings_notify_cb),
                                          background);
    peony_file_monitor_remove (file, background);
    g_signal_handlers_disconnect_by_func (peony_preferences, peony_background_theme_notify_cb,
                                          background);
}

/* key routine that hooks up a background and location */
void
peony_connect_background_to_file_metadata (GtkWidget     *widget,
                                          PeonyFile      *file,
                                          GdkDragAction  default_drag_action)
{
    EelBackground *background;
    gpointer old_file;

    /* Get at the background object we'll be connecting. */
    background = eel_get_widget_background (widget);

    /* Check if it is already connected. */
    old_file = g_object_get_data (G_OBJECT (background), "eel_background_file");
    if (old_file == file)
        return;

    /* Disconnect old signal handlers. */
    if (old_file != NULL)
    {
        g_assert (PEONY_IS_FILE (old_file));

        g_signal_handlers_disconnect_by_func (background,
                                              G_CALLBACK (peony_background_changed_cb), old_file);
        g_signal_handlers_disconnect_by_func (background,
                                              G_CALLBACK (peony_background_reset_cb), old_file);

        g_object_weak_unref (G_OBJECT (background), peony_background_weak_notify, old_file);

        g_signal_handlers_disconnect_by_func (old_file,
                                              G_CALLBACK (peony_background_settings_notify_cb),
                                              background);

        peony_file_monitor_remove (old_file, background);

        g_signal_handlers_disconnect_by_func (peony_preferences, peony_background_theme_notify_cb,
                                              background);
    }

    /* Attach the new directory. */
    peony_file_ref (file);
    g_object_set_data_full (G_OBJECT (background), "eel_background_file",
                            file, (GDestroyNotify) peony_file_unref);

    g_object_set_data (G_OBJECT (background), "default_drag_action",
                       GINT_TO_POINTER (default_drag_action));

    /* Connect new signal handlers. */
    if (file != NULL)
    {
        g_signal_connect_object (background, "settings_changed",
                                 G_CALLBACK (peony_background_changed_cb), file, 0);

        g_signal_connect_object (background, "reset",
                                 G_CALLBACK (peony_background_reset_cb), file, 0);

        g_signal_connect_object (file, "changed",
                                 G_CALLBACK (peony_background_settings_notify_cb), background, 0);

        g_object_weak_ref (G_OBJECT (background), peony_background_weak_notify, file);

        /* arrange to receive file metadata */
        peony_file_monitor_add (file, background, PEONY_FILE_ATTRIBUTE_INFO);

        /* arrange for notification when the theme changes */
        g_signal_connect (peony_preferences, "changed::" PEONY_PREFERENCES_BACKGROUND_SET,
                          G_CALLBACK(peony_background_theme_notify_cb), background);
        g_signal_connect (peony_preferences, "changed::" PEONY_PREFERENCES_BACKGROUND_COLOR,
                          G_CALLBACK(peony_background_theme_notify_cb), background);
        g_signal_connect (peony_preferences, "changed::" PEONY_PREFERENCES_BACKGROUND_URI,
                          G_CALLBACK(peony_background_theme_notify_cb), background);
    }

    /* Update the background based on the file metadata. */
    peony_background_load_from_file_metadata (file, background);
}

/**
 * DESKTOP BACKGROUND HANDLING
 */

/* handle the desktop background "settings_changed" signal */
static void
desktop_background_changed_cb (EelBackground *background,
                               GdkDragAction  action,
                               gpointer       user_data)
{
    eel_bg_save_to_gsettings (background,
                              ukui_background_preferences);
}

/* delayed initializor of desktop background after GSettings changes */
static gboolean
desktop_background_prefs_change_event_idle_cb (EelBackground *background)
{
    eel_bg_load_from_gsettings (background,
                                ukui_background_preferences);

    eel_background_set_color (background,
                              eel_bg_get_desktop_color (background));

    g_object_unref (background);

    return FALSE;       /* remove from the list of event sources */
}

/* handle the desktop background "reset" signal: reset to schema's defaults */
static void
desktop_background_reset_cb (EelBackground *background,
                             gpointer       user_data)
{
    /* Reset to defaults, and save */
    eel_bg_load_from_system_gsettings (background,
                                       ukui_background_preferences,
                                       TRUE);
    /* Reload from saved settings */
    g_idle_add ((GSourceFunc) desktop_background_prefs_change_event_idle_cb,
                g_object_ref (background));
}

/* handle the desktop GSettings "change-event" (batch changes) signal */
static gboolean
desktop_background_prefs_change_event_cb (GSettings *settings,
                                          gpointer   keys,
                                          gint       n_keys,
                                          gpointer   user_data)
{
    EelBackground *background = user_data;

    /* Defer signal processing to avoid making the dconf backend deadlock, and
     * hold a ref to avoid accessing fields of an object that was destroyed.
     */
    g_idle_add ((GSourceFunc) desktop_background_prefs_change_event_idle_cb,
                g_object_ref (background));

    return FALSE;       /* let the event propagate further */
}

static void
desktop_background_weak_notify (gpointer data,
                                GObject *object)
{
    g_signal_handlers_disconnect_by_func (ukui_background_preferences,
                                          G_CALLBACK (desktop_background_prefs_change_event_cb),
                                          object);
}

void
peony_connect_desktop_background_to_settings (PeonyIconContainer *icon_container)
{
    EelBackground *background;

    background = eel_get_widget_background (GTK_WIDGET (icon_container));

    eel_background_set_desktop (background,
                                 TRUE);

    g_signal_connect_object (background, "settings_changed",
                             G_CALLBACK (desktop_background_changed_cb), NULL, 0);

    g_signal_connect_object (background, "reset",
                             G_CALLBACK (desktop_background_reset_cb), NULL, 0);

    eel_bg_load_from_gsettings (background,
                                ukui_background_preferences);

    /* Connect to "change-event" signal to receive *groups of changes* before
     * they are split out into multiple emissions of the "changed" signal.
     */
    g_signal_connect (ukui_background_preferences, "change-event",
                      G_CALLBACK (desktop_background_prefs_change_event_cb),
                      background);

    g_object_weak_ref (G_OBJECT (background),
                       desktop_background_weak_notify, NULL);
}
