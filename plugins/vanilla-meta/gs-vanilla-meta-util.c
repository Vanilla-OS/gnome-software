/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#include "gs-vanilla-meta-util.h"

void
gs_vanilla_meta_app_set_packaging_info(GsApp *app)
{
    g_return_if_fail(GS_IS_APP(app));

    gs_app_set_metadata(app, "GnomeSoftware::SortKey", "200");
    gs_app_set_metadata(app, "GnomeSoftware::PackagingBaseCssColor", "warning_color");
    gs_app_set_metadata(app, "GnomeSoftware::PackagingIcon", "org.vanillaos.FirstSetup-symbolic");
}

/*
 * Retrieve flag to use in subcommand, which is always the suffix of apx_managed,
 * unless it's empty, which means it's apt and no flag is required.
 */
const gchar *
apx_container_flag_from_name(const gchar *container)
{
    g_autoptr(GString) container_flag = g_string_new(container);

    g_string_replace(container_flag, "apx_managed", "", 1);
    if (container_flag->len > 0) {
        // Remove underscore
        g_string_erase(container_flag, 0, 1);

        g_string_prepend(container_flag, "--");
        return g_strdup(container_flag->str);
    } else {
        // Default apt container
        return g_strdup("--apt");
    }
}

/*
 * Runs subprocess and returns an input stream to stdout, or NULL on error.
 */
SubprocessOutput *
gs_vanilla_meta_run_subprocess(const gchar *cmd,
                               GSubprocessFlags flags,
                               GCancellable *cancellable,
                               GError **error)
{
    SubprocessOutput *output;
    output = malloc(sizeof(SubprocessOutput));

    g_autoptr(GSubprocess) subprocess = NULL;
    GInputStream *input_stream;

    subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error, "sh", "-c", cmd, NULL);

    if (subprocess == NULL)
        return NULL;
    if (!g_subprocess_wait(subprocess, cancellable, error))
        return NULL;

    input_stream         = g_subprocess_get_stdout_pipe(subprocess);
    output->input_stream = g_steal_pointer(&input_stream);
    output->exit_code    = g_subprocess_get_exit_status(subprocess);

    return output;
}

/*
 * Gets the "pretty" name from container name (e.g. "apx_managed_aur" returns "AUR")
 */
const gchar *
apx_container_name_to_alias(const gchar *container)
{
    if (!g_strcmp0(container, "apx_managed")) {
        return "APT";
    } else if (!g_strcmp0(container, "apx_managed_aur")) {
        return "AUR";
    } else if (!g_strcmp0(container, "apx_managed_dnf")) {
        return "DNF";
    } else if (!g_strcmp0(container, "apx_managed_apk")) {
        return "APK";
    } else if (!g_strcmp0(container, "apx_managed_zypper")) {
        return "ZYPPER";
    } else if (!g_strcmp0(container, "apx_managed_xbps")) {
        return "XBPS";
    } else {
        return "";
    }
}
