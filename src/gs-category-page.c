/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more category.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-app-list-private.h"
#include "gs-common.h"
#include "gs-summary-tile.h"
#include "gs-popular-tile.h"
#include "gs-category-page.h"

typedef enum {
	SUBCATEGORY_SORT_TYPE_RATING,
	SUBCATEGORY_SORT_TYPE_NAME
} SubcategorySortType;

struct _GsCategoryPage
{
	GsPage		 parent_instance;

	GsPluginLoader	*plugin_loader;
	GtkBuilder	*builder;
	GCancellable	*cancellable;
	GsShell		*shell;
	GsCategory	*category;
	GsCategory	*subcategory;
	guint		sort_rating_handler_id;
	guint		sort_name_handler_id;
	SubcategorySortType sort_type;

	GtkWidget	*infobar_category_shell_extensions;
	GtkWidget	*button_category_shell_extensions;
	GtkWidget	*category_detail_box;
	GtkWidget	*scrolledwindow_category;
	GtkWidget	*subcats_filter_button_label;
	GtkWidget	*subcats_filter_button;
	GtkWidget	*popover_filter_box;
	GtkWidget	*subcats_sort_button_label;
	GtkWidget	*sort_rating_button;
	GtkWidget	*sort_name_button;
	GtkWidget	*featured_grid;
};

G_DEFINE_TYPE (GsCategoryPage, gs_category_page, GS_TYPE_PAGE)

static void
gs_category_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	gtk_widget_show (widget);
	gtk_label_set_label (GTK_LABEL (widget), gs_category_get_name (self->category));
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (self->shell, app);
}

static void
gs_category_page_sort_by_type (GsCategoryPage *self,
			       SubcategorySortType sort_type)
{
	const gchar *button_label;

	if (sort_type == SUBCATEGORY_SORT_TYPE_NAME)
		/* TRANSLATORS: button text when apps have been sorted alphabetically */
		button_label = _("Sorted by Name");
	else
		/* TRANSLATORS: button text when apps have been sorted by their rating */
		button_label = _("Sorted by Rating");

	gtk_label_set_text (GTK_LABEL (self->subcats_sort_button_label), button_label);

	/* only sort again if the sort type is different */
	if (self->sort_type == sort_type)
		return;

	self->sort_type = sort_type;
	gtk_flow_box_invalidate_sort (GTK_FLOW_BOX (self->category_detail_box));
}

static void
sort_button_clicked (GtkButton *button, gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);

	if (button == GTK_BUTTON (self->sort_rating_button))
		gs_category_page_sort_by_type (self, SUBCATEGORY_SORT_TYPE_RATING);
	else
		gs_category_page_sort_by_type (self, SUBCATEGORY_SORT_TYPE_NAME);
}

static void
gs_category_page_get_apps_cb (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
	guint i;
	GsApp *app;
	GtkWidget *tile;
	GsCategoryPage *self = GS_CATEGORY_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* show an empty space for no results */
	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get apps for category apps: %s", error->message);
		return;
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	self->sort_rating_handler_id = g_signal_connect (self->sort_rating_button,
							 "clicked",
							 G_CALLBACK (sort_button_clicked),
							 self);
	self->sort_name_handler_id = g_signal_connect (self->sort_name_button,
						       "clicked",
						       G_CALLBACK (sort_button_clicked),
						       self);

	/* seems a good place */
	gs_shell_profile_dump (self->shell);
}

static gboolean
_max_results_sort_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	return gs_app_get_rating (app1) < gs_app_get_rating (app2);
}

static gint
gs_category_page_sort_flow_box_sort_func (GtkFlowBoxChild *child1,
					  GtkFlowBoxChild *child2,
					  gpointer data)
{
	GsApp *app1 = gs_app_tile_get_app (GS_APP_TILE (gtk_bin_get_child (GTK_BIN (child1))));
	GsApp *app2 = gs_app_tile_get_app (GS_APP_TILE (gtk_bin_get_child (GTK_BIN (child2))));
	SubcategorySortType sort_type;

	if (!GS_IS_APP (app1) || !GS_IS_APP (app2))
		return 0;

	sort_type = GS_CATEGORY_PAGE (data)->sort_type;

	if (sort_type == SUBCATEGORY_SORT_TYPE_RATING) {
		gint rating_app1 = gs_app_get_rating (app1);
		gint rating_app2 = gs_app_get_rating (app2);
		if (rating_app1 > rating_app2)
			return -1;
		if (rating_app1 < rating_app2)
			return 1;
	}
	return g_strcmp0 (gs_app_get_name (app1), gs_app_get_name (app2));
}

static void
gs_category_page_set_featured_placeholders (GsCategoryPage *self)
{
	gs_container_remove_all (GTK_CONTAINER (self->featured_grid));
	for (guint i = 0; i < 3; ++i) {
		GtkWidget *tile = gs_summary_tile_new (NULL);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_grid_attach (GTK_GRID (self->featured_grid), tile, i, 0, 1, 1);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}
	gtk_widget_show (self->featured_grid);
}

static void
gs_category_page_get_featured_apps_cb (GObject *source_object,
				       GAsyncResult *res,
				       gpointer user_data)
{
	GsApp *app;
	GtkWidget *tile;
	GsCategoryPage *self = GS_CATEGORY_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	gs_container_remove_all (GTK_CONTAINER (self->featured_grid));
	gtk_widget_hide (self->featured_grid);

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get featured apps for category apps: %s", error->message);
		return;
	}
	if (gs_app_list_length (list) < 3) {
		g_debug ("not enough featured apps for category %s; not showing featured apps!",
			 gs_category_get_id (self->category));
		return;
	}

	/* randomize so we show different featured apps every time */
	gs_app_list_randomize (list);

	for (guint i = 0; i < 3; ++i) {
		app = gs_app_list_index (list, i);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_grid_attach (GTK_GRID (self->featured_grid), tile, i, 0, 1, 1);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gtk_widget_show (self->featured_grid);
}

static void
gs_category_page_set_featured_apps (GsCategoryPage *self)
{
	GsCategory *featured_subcat = NULL;
	GPtrArray *children = gs_category_get_children (self->category);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	for (guint i = 0; i < children->len; ++i) {
		GsCategory *sub = GS_CATEGORY (g_ptr_array_index (children, i));
		if (g_strcmp0 (gs_category_get_id (sub), "featured") == 0) {
			featured_subcat = sub;
			break;
		}
	}

	if (featured_subcat == NULL)
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					 "category", featured_subcat,
					 "failure-flags", GS_PLUGIN_FAILURE_FLAGS_USE_EVENTS,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_category_page_get_featured_apps_cb,
					    self);
}

static void
gs_category_page_reload (GsPage *page)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
	GtkWidget *tile;
	guint i, count;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (self->subcategory == NULL)
		return;

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_object_unref (self->cancellable);
	}
	self->cancellable = g_cancellable_new ();

	g_debug ("search using %s/%s",
	         gs_category_get_id (self->category),
	         gs_category_get_id (self->subcategory));

	/* show the shell extensions header */
	if (g_strcmp0 (gs_category_get_id (self->category), "addons") == 0 &&
	    g_strcmp0 (gs_category_get_id (self->subcategory), "shell-extensions") == 0) {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, TRUE);
	} else {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, FALSE);
	}

	if (self->sort_rating_handler_id > 0)
		g_signal_handler_disconnect (self->sort_rating_button,
					     self->sort_rating_handler_id);

	if (self->sort_name_handler_id > 0)
		g_signal_handler_disconnect (self->sort_name_button,
					     self->sort_name_handler_id);

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	/* just ensure the sort button has the correct label */
	gs_category_page_sort_by_type (self, self->sort_type);

	count = MIN(30, gs_category_get_size (self->subcategory));
	for (i = 0; i < count; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gs_category_page_set_featured_apps (self);

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					 "category", self->subcategory,
					 "max-results", 50,
					 "filter-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 "failure-flags", GS_PLUGIN_FAILURE_FLAGS_USE_EVENTS,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION,
					 NULL);
	gs_plugin_job_set_sort_func (plugin_job, _max_results_sort_cb);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_category_page_get_apps_cb,
					    self);
}

static void
gs_category_page_populate_filtered (GsCategoryPage *self, GsCategory *subcategory)
{
	g_assert (subcategory != NULL);
	g_set_object (&self->subcategory, subcategory);
	gs_category_page_reload (GS_PAGE (self));
}

static void
filter_button_activated (GtkWidget *button,  gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);
	GsCategory *category;

	category = g_object_get_data (G_OBJECT (button), "category");

	gtk_label_set_text (GTK_LABEL (self->subcats_filter_button_label),
			    gs_category_get_name (category));
	gs_category_page_populate_filtered (self, category);
}

static void
gs_category_page_create_filter_list (GsCategoryPage *self,
                                     GsCategory *category)
{
	GtkWidget *button;
	GsCategory *s;
	guint i;
	GPtrArray *children;
	GtkWidget *first_subcat = NULL;
	gboolean featured_category_found = FALSE;

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));
	gs_container_remove_all (GTK_CONTAINER (self->popover_filter_box));

	children = gs_category_get_children (category);
	for (i = 0; i < children->len; i++) {
		s = GS_CATEGORY (g_ptr_array_index (children, i));
		/* don't include the featured subcategory (those will appear as banners) */
		if (g_strcmp0 (gs_category_get_id (s), "featured") == 0) {
			featured_category_found = TRUE;
			continue;
		}
		if (gs_category_get_size (s) < 1) {
			g_debug ("not showing %s/%s as no apps",
				 gs_category_get_id (category),
				 gs_category_get_id (s));
			continue;
		}
		button = gtk_model_button_new ();
		g_object_set_data_full (G_OBJECT (button), "category", g_object_ref (s), g_object_unref);
		g_object_set (button, "xalign", 0.0, "text", gs_category_get_name (s), NULL);
		gtk_widget_show (button);
		g_signal_connect (button, "clicked", G_CALLBACK (filter_button_activated), self);
		gtk_container_add (GTK_CONTAINER (self->popover_filter_box), button);

		/* make sure the first subcategory gets selected */
		if (first_subcat == NULL)
		        first_subcat = button;
	}
	if (first_subcat != NULL)
		filter_button_activated (first_subcat, self);

	/* set up the placeholders as having the featured category is a good
	 * indicator that there will be featured apps */
	if (featured_category_found) {
		gs_category_page_set_featured_placeholders (self);
	} else {
		gs_container_remove_all (GTK_CONTAINER (self->featured_grid));
		gtk_widget_hide (self->featured_grid);
	}
}

void
gs_category_page_set_category (GsCategoryPage *self, GsCategory *category)
{
	/* this means we've come from the app-view -> back */
	if (self->category == category)
		return;

	/* save this */
	g_clear_object (&self->category);
	self->category = g_object_ref (category);

	/* find apps in this group */
	gs_category_page_create_filter_list (self, category);
}

GsCategory *
gs_category_page_get_category (GsCategoryPage *self)
{
	return self->category;
}

static void
gs_category_page_init (GsCategoryPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_category_page_dispose (GObject *object)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (object);

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->category);
	g_clear_object (&self->subcategory);
	g_clear_object (&self->plugin_loader);

	G_OBJECT_CLASS (gs_category_page_parent_class)->dispose (object);
}

static void
button_shell_extensions_cb (GtkButton *button, GsCategoryPage *self)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	const gchar *argv[] = { "gnome-shell-extension-prefs", NULL };
	ret = g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
			     NULL, NULL, NULL, &error);
	if (!ret)
		g_warning ("failed to exec %s: %s", argv[0], error->message);
}

static gboolean
gs_category_page_setup (GsPage *page,
                        GsShell *shell,
                        GsPluginLoader *plugin_loader,
                        GtkBuilder *builder,
                        GCancellable *cancellable,
                        GError **error)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
	GtkAdjustment *adj;

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);
	self->shell = shell;
	self->sort_type = SUBCATEGORY_SORT_TYPE_RATING;
	gtk_flow_box_set_sort_func (GTK_FLOW_BOX (self->category_detail_box),
				    gs_category_page_sort_flow_box_sort_func,
				    self, NULL);

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_category));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (self->category_detail_box), adj);

	g_signal_connect (self->button_category_shell_extensions, "clicked",
			  G_CALLBACK (button_shell_extensions_cb), self);
	return TRUE;
}

static void
gs_category_page_class_init (GsCategoryPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_category_page_dispose;
	page_class->switch_to = gs_category_page_switch_to;
	page_class->reload = gs_category_page_reload;
	page_class->setup = gs_category_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-category-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, category_detail_box);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, infobar_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, button_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, scrolledwindow_category);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, subcats_filter_button_label);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, subcats_filter_button);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, popover_filter_box);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, subcats_sort_button_label);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, sort_rating_button);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, sort_name_button);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, featured_grid);
}

GsCategoryPage *
gs_category_page_new (void)
{
	GsCategoryPage *self;
	self = g_object_new (GS_TYPE_CATEGORY_PAGE, NULL);
	return self;
}

/* vim: set noexpandtab: */
