/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-app-addon-row.h"

struct _GsAppAddonRow
{
	GtkListBoxRow	 parent_instance;

	GsApp		*app;
	GtkWidget	*name_label;
	GtkWidget	*description_label;
	GtkWidget	*label;
	GtkWidget	*button_remove;
	GtkWidget	*checkbox;
};

G_DEFINE_TYPE (GsAppAddonRow, gs_app_addon_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	PROP_ZERO,
	PROP_SELECTED
};

enum {
	SIGNAL_REMOVE_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static void
checkbox_toggled (GtkWidget *widget, GsAppAddonRow *row)
{
	g_object_notify (G_OBJECT (row), "selected");
}

static void
app_addon_remove_button_cb (GtkWidget *widget, GsAppAddonRow *row)
{
	g_signal_emit (row, signals[SIGNAL_REMOVE_BUTTON_CLICKED], 0);
}

/**
 * gs_app_addon_row_get_summary:
 *
 * Return value: PangoMarkup
 **/
static GString *
gs_app_addon_row_get_summary (GsAppAddonRow *row)
{
	const gchar *tmp = NULL;
	g_autofree gchar *escaped = NULL;

	/* try all these things in order */
	if (gs_app_get_state (row->app) == GS_APP_STATE_UNAVAILABLE)
		tmp = gs_app_get_summary_missing (row->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_summary (row->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_description (row->app);

	escaped = g_markup_escape_text (tmp, -1);
	return g_string_new (escaped);
}

void
gs_app_addon_row_refresh (GsAppAddonRow *row)
{
	g_autoptr(GString) str = NULL;

	if (row->app == NULL)
		return;

	/* join the lines */
	str = gs_app_addon_row_get_summary (row);
	as_gstring_replace (str, "\n", " ");
	gtk_label_set_markup (GTK_LABEL (row->description_label), str->str);
	gtk_label_set_label (GTK_LABEL (row->name_label),
			     gs_app_get_name (row->app));

	/* update the state label */
	switch (gs_app_get_state (row->app)) {
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Pending"));
		break;
	case GS_APP_STATE_PENDING_INSTALL:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Pending install"));
		break;
	case GS_APP_STATE_PENDING_REMOVE:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Pending remove"));
		break;
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_UPDATABLE_LIVE:
	case GS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), C_("Single app", "Installed"));
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Installing"));
		break;
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Removing"));
		break;
	default:
		gtk_widget_set_visible (row->label, FALSE);
		break;
	}

	/* update the checkbox, remove button, and activatable state */
	g_signal_handlers_block_by_func (row->checkbox, checkbox_toggled, row);
	g_signal_handlers_block_by_func (row->checkbox, app_addon_remove_button_cb, row);
	switch (gs_app_get_state (row->app)) {
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_sensitive (row->checkbox, TRUE);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (row->checkbox), TRUE);
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
		break;
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (row->checkbox, TRUE);
		gtk_widget_set_sensitive (row->checkbox, TRUE);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (row->checkbox), FALSE);
		gtk_widget_set_visible (row->button_remove, FALSE);
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
		break;
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_UPDATABLE_LIVE:
	case GS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (row->checkbox, FALSE);
		gtk_widget_set_visible (row->button_remove, !gs_app_has_quirk (row->app, GS_APP_QUIRK_COMPULSORY));
		gtk_widget_set_sensitive (row->button_remove, !gs_app_has_quirk (row->app, GS_APP_QUIRK_COMPULSORY));
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_sensitive (row->checkbox, FALSE);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (row->checkbox), TRUE);
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
		break;
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_visible (row->checkbox, FALSE);
		gtk_widget_set_visible (row->button_remove, TRUE);
		gtk_widget_set_sensitive (row->button_remove, FALSE);
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
		break;
	default:
		gtk_widget_set_sensitive (row->checkbox, FALSE);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (row->checkbox), FALSE);
		gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
		break;
	}
	g_signal_handlers_unblock_by_func (row->checkbox, checkbox_toggled, row);
	g_signal_handlers_unblock_by_func (row->checkbox, app_addon_remove_button_cb, row);
}

GsApp *
gs_app_addon_row_get_addon (GsAppAddonRow *row)
{
	g_return_val_if_fail (GS_IS_APP_ADDON_ROW (row), NULL);
	return row->app;
}

static gboolean
gs_app_addon_row_refresh_idle (gpointer user_data)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (user_data);

	gs_app_addon_row_refresh (row);

	g_object_unref (row);
	return G_SOURCE_REMOVE;
}

static void
gs_app_addon_row_notify_props_changed_cb (GsApp *app,
					  GParamSpec *pspec,
					  GsAppAddonRow *row)
{
	g_idle_add (gs_app_addon_row_refresh_idle, g_object_ref (row));
}

static void
gs_app_addon_row_set_addon (GsAppAddonRow *row, GsApp *app)
{
	row->app = g_object_ref (app);

	g_signal_connect_object (row->app, "notify::state",
				 G_CALLBACK (gs_app_addon_row_notify_props_changed_cb),
				 row, 0);
	gs_app_addon_row_refresh (row);
}

static void
gs_app_addon_row_dispose (GObject *object)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	if (row->app)
		g_signal_handlers_disconnect_by_func (row->app, gs_app_addon_row_notify_props_changed_cb, row);

	g_clear_object (&row->app);

	G_OBJECT_CLASS (gs_app_addon_row_parent_class)->dispose (object);
}

static void
gs_app_addon_row_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		gs_app_addon_row_set_selected (row, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_addon_row_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		g_value_set_boolean (value, gs_app_addon_row_get_selected (row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_addon_row_class_init (GsAppAddonRowClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_app_addon_row_dispose;
	object_class->set_property = gs_app_addon_row_set_property;
	object_class->get_property = gs_app_addon_row_get_property;

	pspec = g_param_spec_boolean ("selected", NULL, NULL,
				      FALSE, G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SELECTED, pspec);

	signals [SIGNAL_REMOVE_BUTTON_CLICKED] =
		g_signal_new ("remove-button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-addon-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, name_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, description_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, checkbox);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, button_remove);
}

static void
gs_app_addon_row_init (GsAppAddonRow *row)
{
	gtk_widget_init_template (GTK_WIDGET (row));

	g_signal_connect (row->checkbox, "toggled",
			  G_CALLBACK (checkbox_toggled), row);
	g_signal_connect (row->button_remove, "clicked",
			  G_CALLBACK (app_addon_remove_button_cb), row);
}

void
gs_app_addon_row_set_selected (GsAppAddonRow *row, gboolean selected)
{
	gtk_check_button_set_active (GTK_CHECK_BUTTON (row->checkbox), selected);
}

gboolean
gs_app_addon_row_get_selected (GsAppAddonRow *row)
{
	return gtk_check_button_get_active (GTK_CHECK_BUTTON (row->checkbox));
}

GtkWidget *
gs_app_addon_row_new (GsApp *app)
{
	GtkWidget *row;

	g_return_val_if_fail (GS_IS_APP (app), NULL);

	row = g_object_new (GS_TYPE_APP_ADDON_ROW, NULL);
	gs_app_addon_row_set_addon (GS_APP_ADDON_ROW (row), app);
	return row;
}
