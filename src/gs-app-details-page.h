/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_DETAILS_PAGE (gs_app_details_page_get_type ())

G_DECLARE_FINAL_TYPE (GsAppDetailsPage, gs_app_details_page, GS, APP_DETAILS_PAGE, GtkBox)

GtkWidget	*gs_app_details_page_new			(void);
GsApp		*gs_app_details_page_get_app			(GsAppDetailsPage	*page);
void		 gs_app_details_page_set_app			(GsAppDetailsPage	*page,
								 GsApp			*app);
gboolean	 gs_app_details_page_get_show_back_button	(GsAppDetailsPage	*page);
void		 gs_app_details_page_set_show_back_button	(GsAppDetailsPage	*page,
								 gboolean		 show_back_button);

G_END_DECLS
