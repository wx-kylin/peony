/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Peony
 *
 *  Copyright (C) 1999, 2000 Red Hat, Inc.
 *  Copyright (C) 2000, 2001 Eazel, Inc.
 *
 *  Peony is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Peony is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Authors: Elliot Lee <sopwith@redhat.com>,
 *           Darin Adler <darin@bentspoon.com>
 *
 */

#include <config.h>
#include "peony-application.h"

#include "file-manager/fm-desktop-icon-view.h"
#include "file-manager/fm-icon-view.h"
#include "file-manager/fm-list-view.h"
#include "file-manager/fm-tree-view.h"
#if ENABLE_EMPTY_VIEW
#include "file-manager/fm-empty-view.h"
#endif /* ENABLE_EMPTY_VIEW */
#include "peony-information-panel.h"
#include "peony-history-sidebar.h"
#include "peony-places-sidebar.h"
#include "peony-notes-viewer.h"
#include "peony-emblem-sidebar.h"
#include "peony-image-properties-page.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "peony-desktop-window.h"
#include "peony-main.h"
#include "peony-spatial-window.h"
#include "peony-navigation-window.h"
#include "peony-window-slot.h"
#include "peony-navigation-window-slot.h"
#include "peony-window-bookmarks.h"
#include "libpeony-private/peony-file-operations.h"
#include "peony-window-private.h"
#include "peony-window-manage-views.h"
#include "peony-freedesktop-dbus.h"
#include <libxml/xmlsave.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-stock-dialogs.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <libpeony-private/peony-debug-log.h>
#include <libpeony-private/peony-file-utilities.h>
#include <libpeony-private/peony-global-preferences.h>
#include <libpeony-private/peony-extensions.h>
#include <libpeony-private/peony-module.h>
#include <libpeony-private/peony-desktop-link-monitor.h>
#include <libpeony-private/peony-directory-private.h>
#include <libpeony-private/peony-signaller.h>
#include <libpeony-extension/peony-menu-provider.h>
#include <libpeony-private/peony-autorun.h>
#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-bg.h>

enum {
	COMMAND_0, /* unused: 0 is an invalid command */

	COMMAND_START_DESKTOP,
	COMMAND_STOP_DESKTOP,
	COMMAND_OPEN_BROWSER,
};

/* Keep window from shrinking down ridiculously small; numbers are somewhat arbitrary */
#define APPLICATION_WINDOW_MIN_WIDTH	300
#define APPLICATION_WINDOW_MIN_HEIGHT	100

#define PEONY_ACCEL_MAP_SAVE_DELAY 30

/* Keeps track of all the desktop windows. */
static GList *peony_application_desktop_windows;

/* Keeps track of all the peony windows. */
static GList *peony_application_window_list;

/* Keeps track of all the object windows */
static GList *peony_application_spatial_window_list;

/* The saving of the accelerator map was requested  */
static gboolean save_of_accel_map_requested = FALSE;

/* File Manager DBus Interface */
static PeonyFreedesktopDBus *fdb_manager = NULL;

static void     desktop_changed_callback          (gpointer                  user_data);
static void     desktop_location_changed_callback (gpointer                  user_data);
static void     mount_removed_callback            (GVolumeMonitor            *monitor,
        GMount                    *mount,
        PeonyApplication       *application);
static void     mount_added_callback              (GVolumeMonitor            *monitor,
        GMount                    *mount,
        PeonyApplication       *application);
static void     volume_added_callback              (GVolumeMonitor           *monitor,
        GVolume                  *volume,
        PeonyApplication      *application);
static void     volume_removed_callback            (GVolumeMonitor           *monitor,
	    GVolume                  *volume,
	    PeonyApplication      *application);
 static void     drive_connected_callback           (GVolumeMonitor           *monitor,
        GDrive                   *drive,
        PeonyApplication      *application);
static void     drive_listen_for_eject_button      (GDrive *drive,
        PeonyApplication *application);
static void     peony_application_load_session     (PeonyApplication *application);
static char *   peony_application_get_session_data (void);

G_DEFINE_TYPE (PeonyApplication, peony_application, G_TYPE_OBJECT);

static gboolean
_unique_message_data_set_geometry_and_uris (UniqueMessageData  *message_data,
        const char *geometry,
        char **uris)
{
    GString *list;
    gint i;
    gchar *result;
    gsize length;

    list = g_string_new (NULL);
    if (geometry != NULL)
    {
        g_string_append (list, geometry);
    }
    g_string_append (list, "\r\n");

    for (i = 0; uris != NULL && uris[i]; i++)
    {
        g_string_append (list, uris[i]);
        g_string_append (list, "\r\n");
    }

    result = g_convert (list->str, list->len,
                        "ASCII", "UTF-8",
                        NULL, &length, NULL);
    g_string_free (list, TRUE);

    if (result)
    {
        unique_message_data_set (message_data, (guchar *) result, length);
        g_free (result);
        return TRUE;
    }

    return FALSE;
}

static gchar **
_unique_message_data_get_geometry_and_uris (UniqueMessageData *message_data,
        char **geometry)
{
    gchar **result = NULL;

    *geometry = NULL;

    gchar *text, *newline, *uris;
    text = unique_message_data_get_text (message_data);
    if (text)
    {
        newline = strchr (text, '\n');
        if (newline)
        {
            *geometry = g_strndup (text, newline-text);
            uris = newline+1;
        }
        else
        {
            uris = text;
        }

        result = g_uri_list_extract_uris (uris);
        g_free (text);
    }

    /* if the string is empty, make it NULL */
    if (*geometry && strlen (*geometry) == 0)
    {
        g_free (*geometry);
        *geometry = NULL;
    }

    return result;
}

GList *
peony_application_get_window_list (void)
{
    return peony_application_window_list;
}

GList *
peony_application_get_spatial_window_list (void)
{
    return peony_application_spatial_window_list;
}

unsigned int
peony_application_get_n_windows (void)
{
    return g_list_length (peony_application_window_list) +
           g_list_length (peony_application_desktop_windows);
}

static void
startup_volume_mount_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
    g_volume_mount_finish (G_VOLUME (source_object), res, NULL);
}

static void
automount_all_volumes (PeonyApplication *application)
{
    GList *volumes, *l;
    GMount *mount;
    GVolume *volume;

    if (g_settings_get_boolean (peony_media_preferences, PEONY_PREFERENCES_MEDIA_AUTOMOUNT))
    {
        /* automount all mountable volumes at start-up */
        volumes = g_volume_monitor_get_volumes (application->volume_monitor);
        for (l = volumes; l != NULL; l = l->next)
        {
            volume = l->data;

            if (!g_volume_should_automount (volume) ||
                    !g_volume_can_mount (volume))
            {
                continue;
            }

            mount = g_volume_get_mount (volume);
            if (mount != NULL)
            {
                g_object_unref (mount);
                continue;
            }

            /* pass NULL as GMountOperation to avoid user interaction */
            g_volume_mount (volume, 0, NULL, NULL, startup_volume_mount_cb, NULL);
        }
    	g_list_free_full (volumes, g_object_unref);
    }

}

static void
smclient_save_state_cb (EggSMClient   *client,
                        GKeyFile      *state_file,
                        PeonyApplication *application)
{
    char *data;

    data = peony_application_get_session_data ();
    if (data)
    {
        g_key_file_set_string (state_file,
                               "Peony",
                               "documents",
                               data);
    }
    g_free (data);
}

static void
smclient_quit_cb (EggSMClient   *client,
                  PeonyApplication *application)
{
    peony_main_event_loop_quit (TRUE);
}

static void
peony_application_init (PeonyApplication *application)
{
    application->unique_app = unique_app_new_with_commands ("org.ukui.Peony", NULL,
                              "start_desktop", COMMAND_START_DESKTOP,
                              "stop_desktop", COMMAND_STOP_DESKTOP,
                              "open_browser", COMMAND_OPEN_BROWSER,
                              NULL);


    application->smclient = egg_sm_client_get ();
    g_signal_connect (application->smclient, "save_state",
                      G_CALLBACK (smclient_save_state_cb),
                      application);
    g_signal_connect (application->smclient, "quit",
                      G_CALLBACK (smclient_quit_cb),
                      application);
    /* TODO: Should connect to quit_requested and block logout on active transfer? */

    /* register views */
    fm_computer_view_register();
    fm_icon_view_register ();
    fm_desktop_icon_view_register ();
    fm_list_view_register ();
    fm_compact_view_register ();
#if ENABLE_EMPTY_VIEW
    fm_empty_view_register ();
#endif /* ENABLE_EMPTY_VIEW */

    /* register sidebars */
   // peony_places_sidebar_register ();
    //peony_information_panel_register ();
    fm_tree_view_register ();
    ///peony_history_sidebar_register ();
    //peony_notes_viewer_register (); /* also property page */
    //peony_emblem_sidebar_register ();

    /* register property pages */
    //peony_image_properties_page_register ();

    /* initialize search path for custom icons */
    gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                       PEONY_DATADIR G_DIR_SEPARATOR_S "icons");
}

PeonyApplication *
peony_application_new (void)
{
    return g_object_new (PEONY_TYPE_APPLICATION, NULL);
}

static void
peony_application_finalize (GObject *object)
{
    PeonyApplication *application;

    application = PEONY_APPLICATION (object);

    peony_bookmarks_exiting ();

    if (application->volume_monitor)
    {
        g_object_unref (application->volume_monitor);
        application->volume_monitor = NULL;
    }

    g_object_unref (application->unique_app);

	if (application->ss_watch_id > 0)
	{
		g_bus_unwatch_name (application->ss_watch_id);
	}
	
	if (application->volume_queue != NULL)
	{
		g_list_free_full (application->volume_queue, g_object_unref);
		application->volume_queue = NULL;
	}

 	if (application->automount_idle_id != 0)
    {
        g_source_remove (application->automount_idle_id);
        application->automount_idle_id = 0;
    }

    if (fdb_manager != NULL)
    {
        g_object_unref (fdb_manager);
        fdb_manager = NULL;
    }

    if (application->ss_proxy != NULL)
    {
		g_object_unref (application->ss_proxy);
		application->ss_proxy = NULL;
	}

    G_OBJECT_CLASS (peony_application_parent_class)->finalize (object);
}

static gboolean
check_required_directories (PeonyApplication *application)
{
    char *user_directory;
    char *desktop_directory;
    GSList *directories;
    gboolean ret;

    g_assert (PEONY_IS_APPLICATION (application));

    ret = TRUE;

    user_directory = peony_get_user_directory ();
    desktop_directory = peony_get_desktop_directory ();

    directories = NULL;

    if (!g_file_test (user_directory, G_FILE_TEST_IS_DIR))
    {
        directories = g_slist_prepend (directories, user_directory);
    }

    if (!g_file_test (desktop_directory, G_FILE_TEST_IS_DIR))
    {
        directories = g_slist_prepend (directories, desktop_directory);
    }

    if (directories != NULL)
    {
        int failed_count;
        GString *directories_as_string;
        GSList *l;
        char *error_string;
        const char *detail_string;
        GtkDialog *dialog;

        ret = FALSE;

        failed_count = g_slist_length (directories);

        directories_as_string = g_string_new ((const char *)directories->data);
        for (l = directories->next; l != NULL; l = l->next)
        {
            g_string_append_printf (directories_as_string, ", %s", (const char *)l->data);
        }

        if (failed_count == 1)
        {
            error_string = g_strdup_printf (_("Peony could not create the required folder \"%s\"."),
                                            directories_as_string->str);
            detail_string = _("Before running Peony, please create the following folder, or "
                              "set permissions such that Peony can create it.");
        }
        else
        {
            error_string = g_strdup_printf (_("Peony could not create the following required folders: "
                                              "%s."), directories_as_string->str);
            detail_string = _("Before running Peony, please create these folders, or "
                              "set permissions such that Peony can create them.");
        }

        dialog = eel_show_error_dialog (error_string, detail_string, NULL);
        /* We need the main event loop so the user has a chance to see the dialog. */
#if GTK_CHECK_VERSION (3, 0, 0)
        peony_main_event_loop_register (GTK_WIDGET (dialog));
#else
        peony_main_event_loop_register (GTK_OBJECT (dialog));
#endif

        g_string_free (directories_as_string, TRUE);
        g_free (error_string);
    }

    g_slist_free (directories);
    g_free (user_directory);
    g_free (desktop_directory);

    return ret;
}

static void
menu_provider_items_updated_handler (PeonyMenuProvider *provider, GtkWidget* parent_window, gpointer data)
{

    g_signal_emit_by_name (peony_signaller_get_current (),
                           "popup_menu_changed");
}

static void
menu_provider_init_callback (void)
{
    GList *providers;
    GList *l;

    providers = peony_extensions_get_for_type (PEONY_TYPE_MENU_PROVIDER);

    for (l = providers; l != NULL; l = l->next)
    {
        PeonyMenuProvider *provider = PEONY_MENU_PROVIDER (l->data);

        g_signal_connect_after (G_OBJECT (provider), "items_updated",
                                (GCallback)menu_provider_items_updated_handler,
                                NULL);
    }

    peony_module_extension_list_free (providers);
}

static gboolean
automount_all_volumes_idle_cb (gpointer data)
{
    PeonyApplication *application = PEONY_APPLICATION (data);

    automount_all_volumes (application);

    application->automount_idle_id = 0;
    return FALSE;
}

static void
mark_desktop_files_trusted (void)
{
    char *do_once_file;
    GFile *f, *c;
    GFileEnumerator *e;
    GFileInfo *info;
    const char *name;
    int fd;

    do_once_file = g_build_filename (g_get_user_data_dir (),
                                     ".converted-launchers", NULL);

    if (g_file_test (do_once_file, G_FILE_TEST_EXISTS))
    {
        goto out;
    }

    f = peony_get_desktop_location ();
    e = g_file_enumerate_children (f,
                                   G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                   G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                   G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE
                                   ,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   NULL, NULL);
    if (e == NULL)
    {
        goto out2;
    }

    while ((info = g_file_enumerator_next_file (e, NULL, NULL)) != NULL)
    {
        name = g_file_info_get_name (info);

        if (g_str_has_suffix (name, ".desktop") &&
                !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE))
        {
            c = g_file_get_child (f, name);
            peony_file_mark_desktop_file_trusted (c,
                                                 NULL, FALSE,
                                                 NULL, NULL);
            g_object_unref (c);
        }
        g_object_unref (info);
    }

    g_object_unref (e);
out2:
    fd = g_creat (do_once_file, 0666);
    close (fd);

    g_object_unref (f);
out:
    g_free (do_once_file);
}

static void
check_volume_queue (PeonyApplication *application)
{
        GList *l, *next;
        GVolume *volume;

        l = application->volume_queue;

        if (application->screensaver_active)
        {
                return;
        }

        while (l != NULL) {
		volume = l->data;
		next = l->next;

		peony_file_operations_mount_volume (NULL, volume, TRUE);
		application->volume_queue =
			g_list_remove (application->volume_queue, volume);

		g_object_unref (volume);
		l = next;
        }

        application->volume_queue = NULL;
}

#define SCREENSAVER_NAME "org.ukui.ScreenSaver"
#define SCREENSAVER_PATH "/org/ukui/ScreenSaver"
#define SCREENSAVER_INTERFACE "org.ukui.ScreenSaver"

static void
screensaver_signal_callback (GDBusProxy *proxy,
                             const gchar *sender_name,
                             const gchar *signal_name,
                             GVariant *parameters,
                             gpointer user_data)
{
	PeonyApplication *application = user_data;

	if (g_strcmp0 (signal_name, "ActiveChanged") == 0)
	{
		g_variant_get (parameters, "(b)", &application->screensaver_active);
		g_debug ("Screensaver active changed to %d", application->screensaver_active);

		check_volume_queue (application);
	}
}

static void
screensaver_get_active_ready_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	PeonyApplication *application = user_data;
	GDBusProxy *proxy = application->ss_proxy;
	GVariant *result;
	GError *error = NULL;

	result = g_dbus_proxy_call_finish (proxy,
					   res,
					   &error);

	if (error != NULL) {
		g_warning ("Can't call GetActive() on the ScreenSaver object: %s",
			   error->message);
		g_error_free (error);

		return;
	}

	g_variant_get (result, "(b)", &application->screensaver_active);
	g_variant_unref (result);

	g_debug ("Screensaver GetActive() returned %d", application->screensaver_active);
}

static void
screensaver_proxy_ready_cb (GObject *source,
			    GAsyncResult *res,
			    gpointer user_data)
{
	PeonyApplication *application = user_data;
	GError *error = NULL;
	GDBusProxy *ss_proxy;
	
	ss_proxy = g_dbus_proxy_new_finish (res, &error);

	if (error != NULL)
	{
		g_warning ("Can't get proxy for the ScreenSaver object: %s",
			   error->message);
		g_error_free (error);

		return;
	}

	g_debug ("ScreenSaver proxy ready");

	application->ss_proxy = ss_proxy;

	g_signal_connect (ss_proxy, "g-signal",
			  G_CALLBACK (screensaver_signal_callback), application);

	g_dbus_proxy_call (ss_proxy,
			   "GetActive",
			   NULL,
			   G_DBUS_CALL_FLAGS_NO_AUTO_START,
			   -1,
			   NULL,
			   screensaver_get_active_ready_cb,
			   application);
}

static void
screensaver_appeared_callback (GDBusConnection *connection,
			       const gchar *name,
			       const gchar *name_owner,
			       gpointer user_data)
{
	PeonyApplication *application = user_data;

	g_debug ("ScreenSaver name appeared");

	application->screensaver_active = FALSE;

	g_dbus_proxy_new (connection,
			  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
			  NULL,
			  name,
			  SCREENSAVER_PATH,
			  SCREENSAVER_INTERFACE,
			  NULL,
			  screensaver_proxy_ready_cb,
			  application);
}

static void
screensaver_vanished_callback (GDBusConnection *connection,
			       const gchar *name,
			       gpointer user_data)
{
	PeonyApplication *application = user_data;

	g_debug ("ScreenSaver name vanished");

	application->screensaver_active = FALSE;
	if (application->ss_proxy != NULL)
	{
		g_object_unref (application->ss_proxy);
		application->ss_proxy = NULL;
	}

	/* in this case force a clear of the volume queue, without
	 * mounting them.
	 */
	if (application->volume_queue != NULL)
	{
		g_list_free_full (application->volume_queue, g_object_unref);
		application->volume_queue = NULL;
	}
}

static void
do_initialize_screensaver (PeonyApplication *application)
{
	application->ss_watch_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION,
				  SCREENSAVER_NAME,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  screensaver_appeared_callback,
				  screensaver_vanished_callback,
				  application,
				  NULL);
}


static void
do_upgrades_once (PeonyApplication *application,
                  gboolean no_desktop)
{
    char *metafile_dir, *updated;
    int fd;

    if (!no_desktop)
    {
        mark_desktop_files_trusted ();
    }

    metafile_dir = g_build_filename(g_get_user_config_dir(), "peony", "metafiles", NULL);

    if (g_file_test (metafile_dir, G_FILE_TEST_IS_DIR))
    {
        updated = g_build_filename (metafile_dir, "migrated-to-gvfs", NULL);
        if (!g_file_test (updated, G_FILE_TEST_EXISTS))
        {
            g_spawn_command_line_async (LIBEXECDIR "/peony-convert-metadata --quiet", NULL);
            fd = g_creat (updated, 0600);
            if (fd != -1)
            {
                close (fd);
            }
        }
        g_free (updated);
    }
    g_free (metafile_dir);
}

static void
finish_startup (PeonyApplication *application,
                gboolean no_desktop)
{
    GList *drives;

    do_upgrades_once (application, no_desktop);

    /* initialize peony modules */
    peony_module_setup ();

    /* attach menu-provider module callback */
    menu_provider_init_callback ();

    /* Initialize the desktop link monitor singleton */
    peony_desktop_link_monitor_get ();

    /* Initialize UKUI screen saver listener to control automount
	 * permission */
	do_initialize_screensaver (application);

 	/* Watch for mounts so we can restore open windows This used
     * to be for showing new window on mount, but is not used
     * anymore */

    /* Watch for unmounts so we can close open windows */
    /* TODO-gio: This should be using the UNMOUNTED feature of GFileMonitor instead */
    application->volume_monitor = g_volume_monitor_get ();
    g_signal_connect_object (application->volume_monitor, "mount_removed",
                             G_CALLBACK (mount_removed_callback), application, 0);
    g_signal_connect_object (application->volume_monitor, "mount_pre_unmount",
                             G_CALLBACK (mount_removed_callback), application, 0);
    g_signal_connect_object (application->volume_monitor, "mount_added",
                             G_CALLBACK (mount_added_callback), application, 0);
    g_signal_connect_object (application->volume_monitor, "volume_added",
                             G_CALLBACK (volume_added_callback), application, 0);
    g_signal_connect_object (application->volume_monitor, "volume_removed",
                             G_CALLBACK (volume_removed_callback), application, 0);
    g_signal_connect_object (application->volume_monitor, "drive_connected",
                             G_CALLBACK (drive_connected_callback), application, 0);

    /* listen for eject button presses */
    drives = g_volume_monitor_get_connected_drives (application->volume_monitor);
    g_list_foreach (drives, (GFunc) drive_listen_for_eject_button, application);
    g_list_free_full (drives, g_object_unref);

    application->automount_idle_id =
        g_idle_add_full (G_PRIORITY_LOW,
                         automount_all_volumes_idle_cb,
                         application, NULL);
}

static void
open_window (PeonyApplication *application,
             const char *startup_id,
             const char *uri, GdkScreen *screen, const char *geometry,
             gboolean browser_window)
{
    GFile *location;
    PeonyWindow *window;

    if (uri == NULL) {
    	location = g_file_new_for_path (g_get_home_dir ());
    } else {
    	location = g_file_new_for_uri (uri);
    }

    if (browser_window ||
            g_settings_get_boolean (peony_preferences, PEONY_PREFERENCES_ALWAYS_USE_BROWSER)) {
        window = peony_application_create_navigation_window (application,
                 startup_id,
                 screen);
    } else {
        window = peony_application_get_spatial_window (application,
                 NULL,
                 startup_id,
                 location,
                 screen,
                 NULL);
    }

    peony_window_go_to (window, location);

    g_object_unref (location);

    if (geometry != NULL && !gtk_widget_get_visible (GTK_WIDGET (window)))
    {
        /* never maximize windows opened from shell if a
         * custom geometry has been requested.
         */
        gtk_window_unmaximize (GTK_WINDOW (window));
        eel_gtk_window_set_initial_geometry_from_string (GTK_WINDOW (window),
                geometry,
                APPLICATION_WINDOW_MIN_WIDTH,
                APPLICATION_WINDOW_MIN_HEIGHT,
                FALSE);
    }
}

static void
open_windows (PeonyApplication *application,
              const char *startup_id,
              char **uris,
              GdkScreen *screen,
              const char *geometry,
              gboolean browser_window)
{
    guint i;

    if (uris == NULL || uris[0] == NULL)
    {
        /* Open a window pointing at the default location. */
        open_window (application, startup_id, NULL, screen, geometry, browser_window);
    }
    else
    {
        /* Open windows at each requested location. */
        for (i = 0; uris[i] != NULL; i++)
        {
            open_window (application, startup_id, uris[i], screen, geometry, browser_window);
        }
    }
}

void
peony_application_open_location (PeonyApplication *application,
                                GFile *location,
                                GFile *selection,
                                const char *startup_id)
{
    PeonyWindow *window;
    GList *sel_list = NULL;

    window = peony_application_create_navigation_window (application, startup_id, gdk_screen_get_default ());

    if (selection != NULL) {
        sel_list = g_list_prepend (NULL, g_object_ref (selection));
    }

    peony_window_slot_open_location_full (peony_window_get_active_slot (window), location,
                                         0, PEONY_WINDOW_OPEN_FLAG_NEW_WINDOW, sel_list, NULL, NULL);

    if (sel_list != NULL) {
        peony_file_list_free (sel_list);
    }
}

static UniqueResponse
message_received_cb (UniqueApp         *unique_app,
                     gint               command,
                     UniqueMessageData *message,
                     guint              time_,
                     gpointer           user_data)
{
    PeonyApplication *application;
    UniqueResponse res;
    char **uris;
    char *geometry;
    GdkScreen *screen;

    application =  user_data;
    res = UNIQUE_RESPONSE_OK;

    switch (command)
    {
    case UNIQUE_CLOSE:
        res = UNIQUE_RESPONSE_OK;
        peony_main_event_loop_quit (TRUE);

        break;
    case UNIQUE_OPEN:
    case COMMAND_OPEN_BROWSER:
        uris = _unique_message_data_get_geometry_and_uris (message, &geometry);
        screen = unique_message_data_get_screen (message);
        open_windows (application,
                      unique_message_data_get_startup_id (message),
                      uris,
                      screen,
                      geometry,
                      command == COMMAND_OPEN_BROWSER);
        g_strfreev (uris);
        g_free (geometry);
        break;
    case COMMAND_START_DESKTOP:
        peony_application_open_desktop (application);
        break;
    case COMMAND_STOP_DESKTOP:
        peony_application_close_desktop ();
        break;
    default:
        res = UNIQUE_RESPONSE_PASSTHROUGH;
        break;
    }

    return res;
}

gboolean
peony_application_save_accel_map (gpointer data)
{
    if (save_of_accel_map_requested)
    {
        char *accel_map_filename;
        accel_map_filename = peony_get_accel_map_file ();
        if (accel_map_filename)
        {
            gtk_accel_map_save (accel_map_filename);
            g_free (accel_map_filename);
        }
        save_of_accel_map_requested = FALSE;
    }

    return FALSE;
}


static void
queue_accel_map_save_callback (GtkAccelMap *object, gchar *accel_path,
                               guint accel_key, GdkModifierType accel_mods,
                               gpointer user_data)
{
    if (!save_of_accel_map_requested)
    {
        save_of_accel_map_requested = TRUE;
        g_timeout_add_seconds (PEONY_ACCEL_MAP_SAVE_DELAY,
                               peony_application_save_accel_map, NULL);
    }
}

static gboolean
desktop_changed_callback_connect (PeonyApplication *application)
{
    g_signal_connect_swapped (peony_preferences, "changed::" PEONY_PREFERENCES_DESKTOP_IS_HOME_DIR,
                              G_CALLBACK(desktop_location_changed_callback),
                              G_OBJECT (application));
    return FALSE;
}

#if GTK_CHECK_VERSION (3, 0, 0)
static void
init_css (void)
{
    GtkCssProvider *provider;
    GError *error = NULL;

    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_path (provider,
				PEONY_DATADIR G_DIR_SEPARATOR_S "peony.css", &error);

    if (error != NULL) {
		g_warning ("Failed to load application css file: %s", error->message);
		g_error_free (error);
    } else {
		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
				GTK_STYLE_PROVIDER (provider),
				GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    g_object_unref (provider);
}
#endif

void
peony_application_startup (PeonyApplication *application,
                          gboolean kill_shell,
                          gboolean no_default_window,
                          gboolean no_desktop,
                          gboolean browser_window,
                          const char *geometry,
                          char **urls)
{
    UniqueMessageData *message;

    /* Check the user's ~/.config/peony directories and post warnings
     * if there are problems.
     */
    if (!kill_shell && !check_required_directories (application))
    {
        return;
    }

    if (kill_shell)
    {
        if (unique_app_is_running (application->unique_app))
        {
            unique_app_send_message (application->unique_app,
                                     UNIQUE_CLOSE, NULL);

        }
    }
    else
    {
        char *accel_map_filename;

        if (!no_desktop &&
            !g_settings_get_boolean (ukui_background_preferences, MATE_BG_KEY_SHOW_DESKTOP))
        {
            no_desktop = TRUE;
        }

        if (!no_desktop)
        {
            if (unique_app_is_running (application->unique_app))
            {
                unique_app_send_message (application->unique_app,
                                         COMMAND_START_DESKTOP, NULL);
            }
            else
            {
                peony_application_open_desktop (application);
            }
        }

        if (!unique_app_is_running (application->unique_app))
        {
            finish_startup (application, no_desktop);
            g_signal_connect (application->unique_app, "message-received", G_CALLBACK (message_received_cb), application);
        }

#if GTK_CHECK_VERSION (3, 0, 0)
        /* initialize CSS theming */
        init_css ();
#endif

        /* Start the File Manager DBus Interface */
        fdb_manager = peony_freedesktop_dbus_new (application);

        /* Monitor the preference to show or hide the desktop */
        g_signal_connect_swapped (ukui_background_preferences,
                                  "changed::" MATE_BG_KEY_SHOW_DESKTOP,
                                  G_CALLBACK(desktop_changed_callback),
                                  G_OBJECT (application));

        /* Monitor the preference to have the desktop */
        /* point to the Unix home folder */
        g_timeout_add_seconds (30, (GSourceFunc) desktop_changed_callback_connect, application);

        /* Create the other windows. */
        if (urls != NULL || !no_default_window)
        {
            if (unique_app_is_running (application->unique_app))
            {
                message = unique_message_data_new ();
                _unique_message_data_set_geometry_and_uris (message, geometry, urls);
                if (browser_window)
                {
                    unique_app_send_message (application->unique_app,
                                             COMMAND_OPEN_BROWSER, message);
                }
                else
                {
                    unique_app_send_message (application->unique_app,
                                             UNIQUE_OPEN, message);
                }
                unique_message_data_free (message);
            }
            else
            {
                open_windows (application, NULL,
                              urls,
                              gdk_display_get_default_screen (gdk_display_get_default()),
                              // gdk_screen_get_default (),
                              geometry,
                              browser_window);
            }
        }

        /* Load session info if availible */
        peony_application_load_session (application);

        /* load accelerator map, and register save callback */
        accel_map_filename = peony_get_accel_map_file ();
        if (accel_map_filename)
        {
            gtk_accel_map_load (accel_map_filename);
            g_free (accel_map_filename);
        }
        g_signal_connect (gtk_accel_map_get (), "changed", G_CALLBACK (queue_accel_map_save_callback), NULL);
    }
}


static void
selection_get_cb (GtkWidget          *widget,
                  GtkSelectionData   *selection_data,
                  guint               info,
                  guint               time)
{
    /* No extra targets atm */
}

static GtkWidget *
get_desktop_manager_selection (GdkDisplay *display)
{
    char selection_name[32];
    GdkAtom selection_atom;
    Window selection_owner;
    GtkWidget *selection_widget;

    g_snprintf (selection_name, sizeof (selection_name), "_NET_DESKTOP_MANAGER_S0");
    selection_atom = gdk_atom_intern (selection_name, FALSE);

    selection_owner = XGetSelectionOwner (GDK_DISPLAY_XDISPLAY (display),
                                          gdk_x11_atom_to_xatom_for_display (display,
                                                  selection_atom));
    if (selection_owner != None)
    {
        return NULL;
    }

    selection_widget = gtk_invisible_new_for_screen (gdk_display_get_default_screen (display));
    /* We need this for gdk_x11_get_server_time() */
    gtk_widget_add_events (selection_widget, GDK_PROPERTY_CHANGE_MASK);

    if (gtk_selection_owner_set_for_display (display,
            selection_widget,
            selection_atom,
            gdk_x11_get_server_time (gtk_widget_get_window (selection_widget))))
    {

        g_signal_connect (selection_widget, "selection_get",
                          G_CALLBACK (selection_get_cb), NULL);
        return selection_widget;
    }

    gtk_widget_destroy (selection_widget);

    return NULL;
}

static void
desktop_unrealize_cb (GtkWidget        *widget,
                      GtkWidget        *selection_widget)
{
    gtk_widget_destroy (selection_widget);
}

static gboolean
selection_clear_event_cb (GtkWidget	        *widget,
                          GdkEventSelection     *event,
                          PeonyDesktopWindow *window)
{
    gtk_widget_destroy (GTK_WIDGET (window));

    peony_application_desktop_windows =
        g_list_remove (peony_application_desktop_windows, window);

    return TRUE;
}

static void
peony_application_create_desktop_windows (PeonyApplication *application)
{
    static gboolean create_in_progress = FALSE;
    GdkDisplay *display;
    PeonyDesktopWindow *window;
    GtkWidget *selection_widget;

    g_return_if_fail (peony_application_desktop_windows == NULL);
    g_return_if_fail (PEONY_IS_APPLICATION (application));

    if (create_in_progress)
    {
        return;
    }

    create_in_progress = TRUE;

    display = gdk_display_get_default ();

    selection_widget = get_desktop_manager_selection (display);
    if (selection_widget != NULL)
    {
        window = peony_desktop_window_new (application, gdk_display_get_default_screen (display));

        g_signal_connect (selection_widget, "selection_clear_event",
                          G_CALLBACK (selection_clear_event_cb), window);

        g_signal_connect (window, "unrealize",
                          G_CALLBACK (desktop_unrealize_cb), selection_widget);

        /* We realize it immediately so that the PEONY_DESKTOP_WINDOW_ID
           property is set so ukui-settings-daemon doesn't try to set the
           background. And we do a gdk_flush() to be sure X gets it. */
        gtk_widget_realize (GTK_WIDGET (window));
        gdk_flush ();


        peony_application_desktop_windows =
            g_list_prepend (peony_application_desktop_windows, window);
    }

    create_in_progress = FALSE;
}

void
peony_application_open_desktop (PeonyApplication *application)
{
    if (peony_application_desktop_windows == NULL)
    {
        peony_application_create_desktop_windows (application);
    }
}

void
peony_application_close_desktop (void)
{
    if (peony_application_desktop_windows != NULL)
    {
        g_list_free_full (peony_application_desktop_windows, (GDestroyNotify) gtk_widget_destroy);
        peony_application_desktop_windows = NULL;
    }
}

void
peony_application_close_all_navigation_windows (void)
{
    GList *list_copy;
    GList *l;

    list_copy = g_list_copy (peony_application_window_list);
    /* First hide all window to get the feeling of quick response */
    for (l = list_copy; l != NULL; l = l->next)
    {
        PeonyWindow *window;

        window = PEONY_WINDOW (l->data);

        if (PEONY_IS_NAVIGATION_WINDOW (window))
        {
            gtk_widget_hide (GTK_WIDGET (window));
        }
    }

    for (l = list_copy; l != NULL; l = l->next)
    {
        PeonyWindow *window;

        window = PEONY_WINDOW (l->data);

        if (PEONY_IS_NAVIGATION_WINDOW (window))
        {
            peony_window_close (window);
        }
    }
    g_list_free (list_copy);
}

static PeonySpatialWindow *
peony_application_get_existing_spatial_window (GFile *location)
{
    GList *l;
    PeonyWindowSlot *slot;
    GFile *window_location;

    for (l = peony_application_get_spatial_window_list ();
            l != NULL; l = l->next) {
        slot = PEONY_WINDOW (l->data)->details->active_pane->active_slot;

        window_location = slot->pending_location;

        if (window_location == NULL) {
        	window_location = slot->location;
        }

        if (window_location != NULL) {
        	if (g_file_equal (location, window_location)) {
            	return PEONY_SPATIAL_WINDOW (l->data);
            }
        }
    }

    return NULL;
}

static PeonySpatialWindow *
find_parent_spatial_window (PeonySpatialWindow *window)
{
    PeonyFile *file;
    PeonyFile *parent_file;
    PeonyWindowSlot *slot;
    GFile *location;

    slot = PEONY_WINDOW (window)->details->active_pane->active_slot;

    location = slot->location;
    if (location == NULL)
    {
        return NULL;
    }
    file = peony_file_get (location);

    if (!file)
    {
        return NULL;
    }

    parent_file = peony_file_get_parent (file);
    peony_file_unref (file);
    while (parent_file)
    {
        PeonySpatialWindow *parent_window;

        location = peony_file_get_location (parent_file);
        parent_window = peony_application_get_existing_spatial_window (location);
        g_object_unref (location);

        /* Stop at the desktop directory if it's not explicitely opened
         * in a spatial window of its own.
         */
        if (peony_file_is_desktop_directory (parent_file) && !parent_window)
        {
            peony_file_unref (parent_file);
            return NULL;
        }

        if (parent_window)
        {
            peony_file_unref (parent_file);
            return parent_window;
        }
        file = parent_file;
        parent_file = peony_file_get_parent (file);
        peony_file_unref (file);
    }

    return NULL;
}

void
peony_application_close_parent_windows (PeonySpatialWindow *window)
{
    PeonySpatialWindow *parent_window;
    PeonySpatialWindow *new_parent_window;

    g_return_if_fail (PEONY_IS_SPATIAL_WINDOW (window));

    parent_window = find_parent_spatial_window (window);

    while (parent_window)
    {

        new_parent_window = find_parent_spatial_window (parent_window);
        peony_window_close (PEONY_WINDOW (parent_window));
        parent_window = new_parent_window;
    }
}

void
peony_application_close_all_spatial_windows (void)
{
    GList *list_copy;
    GList *l;

    list_copy = g_list_copy (peony_application_spatial_window_list);
    /* First hide all window to get the feeling of quick response */
    for (l = list_copy; l != NULL; l = l->next)
    {
        PeonyWindow *window;

        window = PEONY_WINDOW (l->data);

        if (PEONY_IS_SPATIAL_WINDOW (window))
        {
            gtk_widget_hide (GTK_WIDGET (window));
        }
    }

    for (l = list_copy; l != NULL; l = l->next)
    {
        PeonyWindow *window;

        window = PEONY_WINDOW (l->data);

        if (PEONY_IS_SPATIAL_WINDOW (window))
        {
            peony_window_close (window);
        }
    }
    g_list_free (list_copy);
}

static void
#if GTK_CHECK_VERSION (3, 0, 0)
peony_application_destroyed_window (GtkWidget *object, PeonyApplication *application)
#else
peony_application_destroyed_window (GtkObject *object, PeonyApplication *application)
#endif
{
    peony_application_window_list = g_list_remove (peony_application_window_list, object);
}

static gboolean
peony_window_delete_event_callback (GtkWidget *widget,
                                   GdkEvent *event,
                                   gpointer user_data)
{
    PeonyWindow *window;

    window = PEONY_WINDOW (widget);
    peony_window_close (window);

    return TRUE;
}


static PeonyWindow *
create_window (PeonyApplication *application,
               GType window_type,
               const char *startup_id,
               GdkScreen *screen)
{
    PeonyWindow *window;

    g_return_val_if_fail (PEONY_IS_APPLICATION (application), NULL);

    window = PEONY_WINDOW (gtk_widget_new (window_type,
                                          "app", application,
                                          "screen", screen,
                                          NULL));

    if (startup_id)
    {
        gtk_window_set_startup_id (GTK_WINDOW (window), startup_id);
    }

    g_signal_connect_data (window, "delete_event",
                           G_CALLBACK (peony_window_delete_event_callback), NULL, NULL,
                           G_CONNECT_AFTER);

    g_signal_connect_object (window, "destroy",
                             G_CALLBACK (peony_application_destroyed_window), application, 0);

    peony_application_window_list = g_list_prepend (peony_application_window_list, window);

    /* Do not yet show the window. It will be shown later on if it can
     * successfully display its initial URI. Otherwise it will be destroyed
     * without ever having seen the light of day.
     */

    return window;
}

static void
spatial_window_destroyed_callback (void *user_data, GObject *window)
{
    peony_application_spatial_window_list = g_list_remove (peony_application_spatial_window_list, window);

}

PeonyWindow *
peony_application_get_spatial_window (PeonyApplication *application,
                                    PeonyWindow      *requesting_window,
                                    const char      *startup_id,
                                    GFile           *location,
                                    GdkScreen       *screen,
                                    gboolean        *existing)
{
    PeonyWindow *window;
    gchar *uri;

    g_return_val_if_fail (PEONY_IS_APPLICATION (application), NULL);
    window = PEONY_WINDOW
    		(peony_application_get_existing_spatial_window (location));

	if (window != NULL) {
		if (existing != NULL) {
			*existing = TRUE;
        }

		return window;
    }

	if (existing != NULL) {
		*existing = FALSE;
	}

    window = create_window (application, PEONY_TYPE_SPATIAL_WINDOW, startup_id, screen);
    if (requesting_window)
    {
        /* Center the window over the requesting window by default */
        int orig_x, orig_y, orig_width, orig_height;
        int new_x, new_y, new_width, new_height;

        gtk_window_get_position (GTK_WINDOW (requesting_window),
                                 &orig_x, &orig_y);
        gtk_window_get_size (GTK_WINDOW (requesting_window),
                             &orig_width, &orig_height);
        gtk_window_get_default_size (GTK_WINDOW (window),
                                     &new_width, &new_height);

        new_x = orig_x + (orig_width - new_width) / 2;
        new_y = orig_y + (orig_height - new_height) / 2;

        if (orig_width - new_width < 10)
        {
            new_x += 10;
            new_y += 10;
        }

        gtk_window_move (GTK_WINDOW (window), new_x, new_y);
    }

    peony_application_spatial_window_list = g_list_prepend (peony_application_spatial_window_list, window);
    g_object_weak_ref (G_OBJECT (window),
                       spatial_window_destroyed_callback, NULL);

    uri = g_file_get_uri (location);
    peony_debug_log (FALSE, PEONY_DEBUG_LOG_DOMAIN_USER,
                    "present NEW spatial window=%p: %s",
                    window, uri);
    g_free (uri);

    return window;
}

PeonyWindow *
peony_application_create_navigation_window (PeonyApplication *application,
        const char          *startup_id,
        GdkScreen           *screen)
{
    PeonyWindow *window;
    char *geometry_string;
    gboolean maximized;

    g_return_val_if_fail (PEONY_IS_APPLICATION (application), NULL);

    window = create_window (application, PEONY_TYPE_NAVIGATION_WINDOW, startup_id, screen);

    maximized = g_settings_get_boolean (peony_window_state,
                    PEONY_WINDOW_STATE_MAXIMIZED);
    if (maximized)
    {
        gtk_window_maximize (GTK_WINDOW (window));
    }
    else
    {
        gtk_window_unmaximize (GTK_WINDOW (window));
    }

    geometry_string = g_settings_get_string (peony_window_state,
                        PEONY_WINDOW_STATE_GEOMETRY);
    if (geometry_string != NULL &&
            geometry_string[0] != 0)
    {
        eel_gtk_window_set_initial_geometry_from_string
        (GTK_WINDOW (window),
         geometry_string,
         PEONY_NAVIGATION_WINDOW_MIN_WIDTH,
         PEONY_NAVIGATION_WINDOW_MIN_HEIGHT,
         TRUE);
    }
    g_free (geometry_string);

    peony_debug_log (FALSE, PEONY_DEBUG_LOG_DOMAIN_USER,
                    "create new navigation window=%p",
                    window);

    return window;
}

/* callback for changing the directory the desktop points to */
static void
desktop_location_changed_callback (gpointer user_data)
{
    if (peony_application_desktop_windows != NULL)
    {
        g_list_foreach (peony_application_desktop_windows,
                        (GFunc) peony_desktop_window_update_directory, NULL);
    }
}

/* callback for showing or hiding the desktop based on the user's preference */
static void
desktop_changed_callback (gpointer user_data)
{
    PeonyApplication *application;

    application = PEONY_APPLICATION (user_data);
    if (g_settings_get_boolean (ukui_background_preferences, MATE_BG_KEY_SHOW_DESKTOP))
    {
        peony_application_open_desktop (application);
    }
    else
    {
        peony_application_close_desktop ();
    }
}

static gboolean
window_can_be_closed (PeonyWindow *window)
{
    if (!PEONY_IS_DESKTOP_WINDOW (window))
    {
        return TRUE;
    }

    return FALSE;
}

static void
check_screen_lock_and_mount (PeonyApplication *application,
                             GVolume *volume)
{
        if (application->screensaver_active)
        {
                /* queue the volume, to mount it after the screensaver state changed */
                g_debug ("Queuing volume %p", volume);
                application->volume_queue = g_list_prepend (application->volume_queue,
                                                              g_object_ref (volume));
        } else {
                /* mount it immediately */
		peony_file_operations_mount_volume (NULL, volume, TRUE);
        }       
}

static void
volume_removed_callback (GVolumeMonitor *monitor,
                         GVolume *volume,
                         PeonyApplication *application)
{
        g_debug ("Volume %p removed, removing from the queue", volume);

        /* clear it from the queue, if present */
        application->volume_queue =
                g_list_remove (application->volume_queue, volume);
}

static void
volume_added_callback (GVolumeMonitor *monitor,
                       GVolume *volume,
                       PeonyApplication *application)
{
    if (g_settings_get_boolean (peony_media_preferences, PEONY_PREFERENCES_MEDIA_AUTOMOUNT) &&
            g_volume_should_automount (volume) &&
            g_volume_can_mount (volume))
    {
        check_screen_lock_and_mount (application, volume);
    }
    else
    {
        /* Allow peony_autorun() to run. When the mount is later
         * added programmatically (i.e. for a blank CD),
         * peony_autorun() will be called by mount_added_callback(). */
        peony_allow_autorun_for_volume (volume);
        peony_allow_autorun_for_volume_finish (volume);
    }
}

static void
drive_eject_cb (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
    GError *error;
    char *primary;
    char *name;
    error = NULL;
    if (!g_drive_eject_with_operation_finish (G_DRIVE (source_object), res, &error))
    {
        if (error->code != G_IO_ERROR_FAILED_HANDLED)
        {
            name = g_drive_get_name (G_DRIVE (source_object));
            primary = g_strdup_printf (_("Unable to eject %s"), name);
            g_free (name);
            eel_show_error_dialog (primary,
                                   error->message,
                                   NULL);
            g_free (primary);
        }
        g_error_free (error);
    }
}

static void
drive_eject_button_pressed (GDrive *drive,
                            PeonyApplication *application)
{
    GMountOperation *mount_op;

    mount_op = gtk_mount_operation_new (NULL);
    g_drive_eject_with_operation (drive, 0, mount_op, NULL, drive_eject_cb, NULL);
    g_object_unref (mount_op);
}

static void
drive_listen_for_eject_button (GDrive *drive, PeonyApplication *application)
{
    g_signal_connect (drive,
                      "eject-button",
                      G_CALLBACK (drive_eject_button_pressed),
                      application);
}

static void
drive_connected_callback (GVolumeMonitor *monitor,
                          GDrive *drive,
                          PeonyApplication *application)
{
    drive_listen_for_eject_button (drive, application);
}

static void
autorun_show_window (GMount *mount, gpointer user_data)
{
    GFile *location;
    PeonyApplication *application = user_data;
    PeonyWindow *window;
    gboolean existing;

    location = g_mount_get_root (mount);
    existing = FALSE;

    /* There should probably be an easier way to do this */
    if (g_settings_get_boolean (peony_preferences, PEONY_PREFERENCES_ALWAYS_USE_BROWSER)) {
        window = peony_application_create_navigation_window (application,
                                                            NULL,
                                                            gdk_screen_get_default ());
    }
    else
    {
        window = peony_application_get_spatial_window (application,
                                                      NULL,
                                                      NULL,
                                                      location,
                                                      gdk_screen_get_default (),
                                                      NULL);
    }

    peony_window_go_to (window, location);

    g_object_unref (location);
}

static void
mount_added_callback (GVolumeMonitor *monitor,
                      GMount *mount,
                      PeonyApplication *application)
{
    PeonyDirectory *directory;
    GFile *root;

    root = g_mount_get_root (mount);
    directory = peony_directory_get_existing (root);
    g_object_unref (root);
    if (directory != NULL)
    {
        peony_directory_force_reload (directory);
        peony_directory_unref (directory);
    }

    peony_autorun (mount, autorun_show_window, application);
}

static PeonyWindowSlot *
get_first_navigation_slot (GList *slot_list)
{
    GList *l;

    for (l = slot_list; l != NULL; l = l->next)
    {
        if (PEONY_IS_NAVIGATION_WINDOW_SLOT (l->data))
        {
            return l->data;
        }
    }

    return NULL;
}

/* We redirect some slots and close others */
static gboolean
should_close_slot_with_mount (PeonyWindow *window,
                              PeonyWindowSlot *slot,
                              GMount *mount)
{
    if (PEONY_IS_SPATIAL_WINDOW (window))
    {
        return TRUE;
    }
    return peony_navigation_window_slot_should_close_with_mount (PEONY_NAVIGATION_WINDOW_SLOT (slot),
            mount);
}

/* Called whenever a mount is unmounted. Check and see if there are
 * any windows open displaying contents on the mount. If there are,
 * close them.  It would also be cool to save open window and position
 * info.
 *
 * This is also called on pre_unmount.
 */
static void
mount_removed_callback (GVolumeMonitor *monitor,
                        GMount *mount,
                        PeonyApplication *application)
{
    GList *window_list, *node, *close_list;
    PeonyWindow *window;
    PeonyWindowSlot *slot;
    PeonyWindowSlot *force_no_close_slot;
    GFile *root, *computer;
    gboolean unclosed_slot;

    close_list = NULL;
    force_no_close_slot = NULL;
    unclosed_slot = FALSE;

    /* Check and see if any of the open windows are displaying contents from the unmounted mount */
    window_list = peony_application_get_window_list ();

    root = g_mount_get_root (mount);
    /* Construct a list of windows to be closed. Do not add the non-closable windows to the list. */
    for (node = window_list; node != NULL; node = node->next)
    {
        window = PEONY_WINDOW (node->data);
        if (window != NULL && window_can_be_closed (window))
        {
            GList *l;
            GList *lp;
            GFile *location;

            for (lp = window->details->panes; lp != NULL; lp = lp->next)
            {
                PeonyWindowPane *pane;
                pane = (PeonyWindowPane*) lp->data;
                for (l = pane->slots; l != NULL; l = l->next)
                {
                    slot = l->data;
                    location = slot->location;
                    if (g_file_has_prefix (location, root) ||
                            g_file_equal (location, root))
                    {
                        close_list = g_list_prepend (close_list, slot);

                        if (!should_close_slot_with_mount (window, slot, mount))
                        {
                            /* We'll be redirecting this, not closing */
                            unclosed_slot = TRUE;
                        }
                    }
                    else
                    {
                        unclosed_slot = TRUE;
                    }
                } /* for all slots */
            } /* for all panes */
        }
    }

    if (peony_application_desktop_windows == NULL &&
            !unclosed_slot)
    {
        /* We are trying to close all open slots. Keep one navigation slot open. */
        force_no_close_slot = get_first_navigation_slot (close_list);
    }

    /* Handle the windows in the close list. */
    for (node = close_list; node != NULL; node = node->next)
    {
        slot = node->data;
        window = slot->pane->window;

        if (should_close_slot_with_mount (window, slot, mount) &&
                slot != force_no_close_slot)
        {
            peony_window_slot_close (slot);
        }
        else
        {
            computer = g_file_new_for_uri ("computer:///");
            peony_window_slot_go_to (slot, computer, FALSE);
            g_object_unref(computer);
        }
    }

    g_list_free (close_list);
}

static char *
icon_to_string (GIcon *icon)
{
    const char * const *names;
    GFile *file;

    if (icon == NULL)
    {
        return NULL;
    }
    else if (G_IS_THEMED_ICON (icon))
    {
        names = g_themed_icon_get_names (G_THEMED_ICON (icon));
        return g_strjoinv (":", (char **)names);
    }
    else if (G_IS_FILE_ICON (icon))
    {
        file = g_file_icon_get_file (G_FILE_ICON (icon));
        return g_file_get_path (file);
    }
    return NULL;
}

static GIcon *
icon_from_string (const char *string)
{
    GFile *file;
    GIcon *icon;
    gchar **names;

    if (g_path_is_absolute (string))
    {
        file = g_file_new_for_path (string);
        icon = g_file_icon_new (file);
        g_object_unref (file);
        return icon;
    }
    else
    {
        names = g_strsplit (string, ":", 0);
        icon = g_themed_icon_new_from_names (names, -1);
        g_strfreev (names);
        return icon;
    }
    return NULL;
}

static char *
peony_application_get_session_data (void)
{
    xmlDocPtr doc;
    xmlNodePtr root_node, history_node;
    GList *l;
    char *data;
    unsigned n_processed;
    xmlSaveCtxtPtr ctx;
    xmlBufferPtr buffer;

    doc = xmlNewDoc ("1.0");

    root_node = xmlNewNode (NULL, "session");
    xmlDocSetRootElement (doc, root_node);

    history_node = xmlNewChild (root_node, NULL, "history", NULL);

    n_processed = 0;
    for (l = peony_get_history_list (); l != NULL; l = l->next)
    {
        PeonyBookmark *bookmark;
        xmlNodePtr bookmark_node;
        GIcon *icon;
        char *tmp;

        bookmark = l->data;

        bookmark_node = xmlNewChild (history_node, NULL, "bookmark", NULL);

        tmp = peony_bookmark_get_name (bookmark);
        xmlNewProp (bookmark_node, "name", tmp);
        g_free (tmp);

        icon = peony_bookmark_get_icon (bookmark);
        tmp = icon_to_string (icon);
        g_object_unref (icon);
        if (tmp)
        {
            xmlNewProp (bookmark_node, "icon", tmp);
            g_free (tmp);
        }

        tmp = peony_bookmark_get_uri (bookmark);
        xmlNewProp (bookmark_node, "uri", tmp);
        g_free (tmp);

        if (peony_bookmark_get_has_custom_name (bookmark))
        {
            xmlNewProp (bookmark_node, "has_custom_name", "TRUE");
        }

        if (++n_processed > 50)   /* prevent history list from growing arbitrarily large. */
        {
            break;
        }
    }

    for (l = peony_application_window_list; l != NULL; l = l->next)
    {
        xmlNodePtr win_node, slot_node;
        PeonyWindow *window;
        PeonyWindowSlot *slot, *active_slot;
        GList *slots, *m;
        char *tmp;

        window = l->data;

        win_node = xmlNewChild (root_node, NULL, "window", NULL);

        xmlNewProp (win_node, "type", PEONY_IS_NAVIGATION_WINDOW (window) ? "navigation" : "spatial");

        if (PEONY_IS_NAVIGATION_WINDOW (window))   /* spatial windows store their state as file metadata */
        {
            GdkWindow *gdk_window;

            tmp = eel_gtk_window_get_geometry_string (GTK_WINDOW (window));
            xmlNewProp (win_node, "geometry", tmp);
            g_free (tmp);

            gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

            if (gdk_window &&
                    gdk_window_get_state (gdk_window) & GDK_WINDOW_STATE_MAXIMIZED)
            {
                xmlNewProp (win_node, "maximized", "TRUE");
            }

            if (gdk_window &&
                    gdk_window_get_state (gdk_window) & GDK_WINDOW_STATE_STICKY)
            {
                xmlNewProp (win_node, "sticky", "TRUE");
            }

            if (gdk_window &&
                    gdk_window_get_state (gdk_window) & GDK_WINDOW_STATE_ABOVE)
            {
                xmlNewProp (win_node, "keep-above", "TRUE");
            }
        }

        slots = peony_window_get_slots (window);
        active_slot = peony_window_get_active_slot (window);

        /* store one slot as window location. Otherwise
         * older Peony versions will bail when reading the file. */
        tmp = peony_window_slot_get_location_uri (active_slot);
        xmlNewProp (win_node, "location", tmp);
        g_free (tmp);

        for (m = slots; m != NULL; m = m->next)
        {
            slot = PEONY_WINDOW_SLOT (m->data);

            slot_node = xmlNewChild (win_node, NULL, "slot", NULL);

            tmp = peony_window_slot_get_location_uri (slot);
            xmlNewProp (slot_node, "location", tmp);
            g_free (tmp);

            if (slot == active_slot)
            {
                xmlNewProp (slot_node, "active", "TRUE");
            }
        }

        g_list_free (slots);
    }

    buffer = xmlBufferCreate ();
    xmlIndentTreeOutput = 1;
    ctx = xmlSaveToBuffer (buffer, "UTF-8", XML_SAVE_FORMAT);
    if (xmlSaveDoc (ctx, doc) < 0 ||
            xmlSaveFlush (ctx) < 0)
    {
        g_message ("failed to save session");
    }

    xmlSaveClose(ctx);
    data = g_strndup (buffer->content, buffer->use);
    xmlBufferFree (buffer);

    xmlFreeDoc (doc);

    return data;
}

void
peony_application_load_session (PeonyApplication *application)
{
    xmlDocPtr doc;
    gboolean bail;
    xmlNodePtr root_node;
    GKeyFile *state_file;
    char *data;

    if (!egg_sm_client_is_resumed (application->smclient))
    {
        return;
    }

    state_file = egg_sm_client_get_state_file (application->smclient);
    if (!state_file)
    {
        return;
    }

    data = g_key_file_get_string (state_file,
                                  "Peony",
                                  "documents",
                                  NULL);
    if (data == NULL)
    {
        return;
    }

    bail = TRUE;

    doc = xmlReadMemory (data, strlen (data), NULL, "UTF-8", 0);
    if (doc != NULL && (root_node = xmlDocGetRootElement (doc)) != NULL)
    {
        xmlNodePtr node;

        bail = FALSE;

        for (node = root_node->children; node != NULL; node = node->next)
        {

            if (g_strcmp0 (node->name, "text") == 0)
            {
                continue;
            }
            else if (g_strcmp0 (node->name, "history") == 0)
            {
                xmlNodePtr bookmark_node;
                gboolean emit_change;

                emit_change = FALSE;

                for (bookmark_node = node->children; bookmark_node != NULL; bookmark_node = bookmark_node->next)
                {
                    if (g_strcmp0 (bookmark_node->name, "text") == 0)
                    {
                        continue;
                    }
                    else if (g_strcmp0 (bookmark_node->name, "bookmark") == 0)
                    {
                        xmlChar *name, *icon_str, *uri;
                        gboolean has_custom_name;
                        GIcon *icon;
                        GFile *location;

                        uri = xmlGetProp (bookmark_node, "uri");
                        name = xmlGetProp (bookmark_node, "name");
                        has_custom_name = xmlHasProp (bookmark_node, "has_custom_name") ? TRUE : FALSE;
                        icon_str = xmlGetProp (bookmark_node, "icon");
                        icon = NULL;
                        if (icon_str)
                        {
                            icon = icon_from_string (icon_str);
                        }
                        location = g_file_new_for_uri (uri);

                        emit_change |= peony_add_to_history_list_no_notify (location, name, has_custom_name, icon);

                        g_object_unref (location);

                        if (icon)
                        {
                            g_object_unref (icon);
                        }
                        xmlFree (name);
                        xmlFree (uri);
                        xmlFree (icon_str);
                    }
                    else
                    {
                        g_message ("unexpected bookmark node %s while parsing session data", bookmark_node->name);
                        bail = TRUE;
                        continue;
                    }
                }

                if (emit_change)
                {
                    peony_send_history_list_changed ();
                }
            }
            else if (g_strcmp0 (node->name, "window") == 0)
            {
                PeonyWindow *window;
                xmlChar *type, *location_uri, *slot_uri;
                xmlNodePtr slot_node;
                GFile *location;
                int i;

                type = xmlGetProp (node, "type");
                if (type == NULL)
                {
                    g_message ("empty type node while parsing session data");
                    bail = TRUE;
                    continue;
                }

                location_uri = xmlGetProp (node, "location");
                if (location_uri == NULL)
                {
                    g_message ("empty location node while parsing session data");
                    bail = TRUE;
                    xmlFree (type);
                    continue;
                }

                if (g_strcmp0 (type, "navigation") == 0)
                {
                    xmlChar *geometry;

                    window = peony_application_create_navigation_window (application, NULL, gdk_screen_get_default ());

                    geometry = xmlGetProp (node, "geometry");
                    if (geometry != NULL)
                    {
                        eel_gtk_window_set_initial_geometry_from_string
                        (GTK_WINDOW (window),
                         geometry,
                         PEONY_NAVIGATION_WINDOW_MIN_WIDTH,
                         PEONY_NAVIGATION_WINDOW_MIN_HEIGHT,
                         FALSE);
                    }
                    xmlFree (geometry);

                    if (xmlHasProp (node, "maximized"))
                    {
                        gtk_window_maximize (GTK_WINDOW (window));
                    }
                    else
                    {
                        gtk_window_unmaximize (GTK_WINDOW (window));
                    }

                    if (xmlHasProp (node, "sticky"))
                    {
                        gtk_window_stick (GTK_WINDOW (window));
                    }
                    else
                    {
                        gtk_window_unstick (GTK_WINDOW (window));
                    }

                    if (xmlHasProp (node, "keep-above"))
                    {
                        gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
                    }
                    else
                    {
                        gtk_window_set_keep_above (GTK_WINDOW (window), FALSE);
                    }

                    for (i = 0, slot_node = node->children; slot_node != NULL; slot_node = slot_node->next)
                    {
                        if (g_strcmp0 (slot_node->name, "slot") == 0)
                        {
                            slot_uri = xmlGetProp (slot_node, "location");
                            if (slot_uri != NULL)
                            {
                                PeonyWindowSlot *slot;

                                if (i == 0)
                                {
                                    slot = window->details->active_pane->active_slot;
                                }
                                else
                                {
                                    slot = peony_window_open_slot (window->details->active_pane, PEONY_WINDOW_OPEN_SLOT_APPEND);
                                }

                                location = g_file_new_for_uri (slot_uri);
                                peony_window_slot_open_location (slot, location, FALSE);

                                if (xmlHasProp (slot_node, "active"))
                                {
                                    peony_window_set_active_slot (slot->pane->window, slot);
                                }

                                i++;
                            }
                            xmlFree (slot_uri);
                        }
                    }

                    if (i == 0)
                    {
                        /* This may be an old session file */
                        location = g_file_new_for_uri (location_uri);
                        peony_window_slot_open_location (window->details->active_pane->active_slot, location, FALSE);
                        g_object_unref (location);
                    }
                }
                else if (g_strcmp0 (type, "spatial") == 0)
                {
                    location = g_file_new_for_uri (location_uri);
                    window = peony_application_get_spatial_window (application, NULL, NULL, 
                    											  location, gdk_screen_get_default (),
                    											  NULL);

					peony_window_go_to (window, location);

                    g_object_unref (location);
                }
                else
                {
                    g_message ("unknown window type \"%s\" while parsing session data", type);
                    bail = TRUE;
                }

                xmlFree (type);
                xmlFree (location_uri);
            }
            else
            {
                g_message ("unexpected node %s while parsing session data", node->name);
                bail = TRUE;
                continue;
            }
        }
    }

    if (doc != NULL)
    {
        xmlFreeDoc (doc);
    }

    g_free (data);

    if (bail)
    {
        g_message ("failed to load session");
    }
}

static void
peony_application_class_init (PeonyApplicationClass *class)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (class);
    object_class->finalize = peony_application_finalize;
}