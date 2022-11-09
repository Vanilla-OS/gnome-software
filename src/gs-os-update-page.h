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

#define GS_TYPE_OS_UPDATE_PAGE (gs_os_update_page_get_type ())

G_DECLARE_FINAL_TYPE (GsOsUpdatePage, gs_os_update_page, GS, OS_UPDATE_PAGE, GtkBox)

GtkWidget	*gs_os_update_page_new		(void);
GsApp		*gs_os_update_page_get_app	(GsOsUpdatePage	*page);
void		 gs_os_update_page_set_app	(GsOsUpdatePage	*page,
						 GsApp		*app);
gboolean	 gs_os_update_page_get_show_back_button
						(GsOsUpdatePage *page);
void		 gs_os_update_page_set_show_back_button
						(GsOsUpdatePage *page,
						 gboolean show_back_button);

G_END_DECLS
