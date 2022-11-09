/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_INSTALLED_PAGE (gs_installed_page_get_type ())

G_DECLARE_FINAL_TYPE (GsInstalledPage, gs_installed_page, GS, INSTALLED_PAGE, GsPage)

GsInstalledPage	*gs_installed_page_new			(void);

gboolean	 gs_installed_page_get_is_narrow	(GsInstalledPage	*self);
void		 gs_installed_page_set_is_narrow	(GsInstalledPage	*self,
							 gboolean		 is_narrow);

G_END_DECLS
