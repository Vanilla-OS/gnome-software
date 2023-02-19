/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include "gnome-software-private.h"
#include "gs-tariff-editor.h"

G_BEGIN_DECLS

#define GS_TYPE_PREFS_DIALOG (gs_prefs_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsPrefsDialog, gs_prefs_dialog, GS, PREFS_DIALOG, AdwPreferencesWindow)

GtkWidget	     *gs_prefs_dialog_new			 (GtkWindow	    *parent,
								  GsPluginLoader    *plugin_loader);

static void          on_automatic_updates_switch_changed_cb      (GtkSwitch      *sw,
                                                                  GParamSpec     *pspec,
                                                                  GsPrefsDialog *self);

static gboolean      on_change_network_link_activated_cb         (GtkLabel       *label,
								  gchar          *uri,
				     				  GsPrefsDialog *self);

static void          on_network_changed_cb                       (GsPrefsDialog *self);

static void          on_network_changes_committed_cb             (GObject        *source,
                                                                  GAsyncResult   *result,
                                                                  gpointer        user_data);

static void          on_scheduled_updates_switch_changed_cb      (GtkSwitch      *sw,
                                                                  GParamSpec     *pspec,
                                                                  GsPrefsDialog *self);

static void          on_tariff_changed_cb                        (GsTariffEditor *tariff_editor,
                                                                  GsPrefsDialog *self);

static gboolean      save_tariff_cb                              (gpointer        user_data);

G_END_DECLS
