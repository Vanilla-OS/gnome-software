/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#pragma once

#include <glib.h>
#include <gnome-software.h>

G_BEGIN_DECLS

typedef struct _SubprocessOutput {
    GInputStream *input_stream;
    gint exit_code;
} SubprocessOutput;

void gs_vanilla_meta_app_set_packaging_info(GsApp *app);
const gchar *apx_container_flag_from_name(const gchar *container);
SubprocessOutput *gs_vanilla_meta_run_subprocess(const gchar *cmd,
                                                 GSubprocessFlags flags,
                                                 GCancellable *cancellable,
                                                 GError **error);
const gchar *apx_container_name_to_alias(const gchar *container);

G_END_DECLS
