/* gs-tariff-editor.c
 *
 * Copyright © 2018 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <gtk/gtk.h>
#include <libmogwai-tariff/tariff.h>

G_BEGIN_DECLS

typedef enum
{
  GS_TARIFF_EDITOR_ERROR_NOT_SUPPORTED,
  GS_TARIFF_EDITOR_ERROR_WRONG_FORMAT,
} GsTariffEditorError;

#define GS_TARIFF_EDITOR_ERROR (gs_tariff_editor_error_quark ())
#define GS_TYPE_TARIFF_EDITOR  (gs_tariff_editor_get_type())

G_DECLARE_FINAL_TYPE (GsTariffEditor, gs_tariff_editor, GS, TARIFF_EDITOR, GtkGrid)

GQuark    gs_tariff_editor_error_quark           (void);

GVariant* gs_tariff_editor_get_tariff_as_variant (GsTariffEditor  *self);

void      gs_tariff_editor_load_tariff           (GsTariffEditor  *self,
                                                  MwtTariff       *tariff,
                                                  GError         **error);

G_END_DECLS
