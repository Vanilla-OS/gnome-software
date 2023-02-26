/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#include <config.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gnome-software.h>
#include <stdlib.h>
#include <xmlb.h>

#include "gs-appstream.h"
#include "gs-plugin-vanilla-meta.h"
#include "gs-vanilla-meta-util.h"

static gint get_priority_for_interactivity(gboolean interactive);
static gboolean plugin_vanillameta_pick_apx_desktop_file_cb(GsPlugin *plugin,
                                                            GsApp *app,
                                                            const gchar *filename,
                                                            GKeyFile *key_file);
static gboolean
refresh_plugin_cache(GsPluginVanillaMeta *self, GCancellable *cancellable, GError **error);
static gboolean refine_app(GsPluginVanillaMeta *self,
                           GsApp *app,
                           GsPluginRefineFlags flags,
                           GCancellable *cancellable,
                           GError **error);
static void
setup_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void enable_repository_thread_cb(GTask *task,
                                        gpointer source_object,
                                        gpointer task_data,
                                        GCancellable *cancellable);
gboolean check_app_is_installed(GsApp *app,
                                GCancellable *cancellable,
                                GError *error,
                                gboolean update_status);
static void list_apps_thread_cb(GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable);
static void refine_thread_cb(GTask *task,
                             gpointer source_object,
                             gpointer task_data,
                             GCancellable *cancellable);

const gchar *gz_metadata_filename   = "/usr/share/swcatalog/xml/vanillaos-kinetic-main.xml.gz";
const gchar *metadata_silo_filename = ".cache/vanilla_meta/metadata.xmlb";

struct _GsPluginVanillaMeta {
    GsPlugin parent;
    GsWorkerThread *worker; /* (owned) */
    GMutex silo_mutex;
    XbSilo *silo;
};

G_DEFINE_TYPE(GsPluginVanillaMeta, gs_plugin_vanilla_meta, GS_TYPE_PLUGIN)

#define assert_in_worker(self) g_assert(gs_worker_thread_is_in_worker_context(self->worker))

static void
gs_plugin_vanilla_meta_dispose(GObject *object)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(object);

    g_clear_object(&self->worker);
    g_mutex_clear(&self->silo_mutex);
    G_OBJECT_CLASS(gs_plugin_vanilla_meta_parent_class)->dispose(object);
}

static void
gs_plugin_vanilla_meta_finalize(GObject *object)
{
    G_OBJECT_CLASS(gs_plugin_vanilla_meta_parent_class)->finalize(object);
}

static gint
get_priority_for_interactivity(gboolean interactive)
{
    return interactive ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW;
}

static void
gs_plugin_vanilla_meta_setup_async(GsPlugin *plugin,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);
    g_autoptr(GTask) task     = NULL;

    g_mutex_init(&self->silo_mutex);

    task = g_task_new(plugin, cancellable, callback, user_data);
    g_task_set_source_tag(task, gs_plugin_vanilla_meta_setup_async);

    // Start up a worker thread to process all the plugin's function calls.
    self->worker = gs_worker_thread_new("gs-plugin-vanilla-meta");

    gs_worker_thread_queue(self->worker, G_PRIORITY_DEFAULT, setup_thread_cb,
                           g_steal_pointer(&task));
}

static void
setup_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    GsPluginVanillaMeta *self         = GS_PLUGIN_VANILLA_META(source_object);
    const gchar *const *locales       = g_get_language_names();
    g_autoptr(XbBuilder) builder      = xb_builder_new();
    g_autoptr(XbBuilderNode) info     = NULL;
    g_autoptr(XbBuilderSource) source = xb_builder_source_new();
    g_autoptr(GFile) silo_file        = g_file_new_for_path(metadata_silo_filename);
    g_autoptr(GFile) metadata_file    = g_file_new_for_path(gz_metadata_filename);
    g_autoptr(GError) error           = NULL;

    assert_in_worker(self);

    g_debug("Loading app silo");

    // Add current locales
    for (guint i = 0; locales[i] != NULL; i++)
        xb_builder_add_locale(builder, locales[i]);

    // Load file into silo
    if (!xb_builder_source_load_file(source, metadata_file,
                                     XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
                                         XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                     cancellable, &error)) {
        g_debug("Failed to load xml file for builder");
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    info = xb_builder_node_insert(NULL, "info", NULL);
    xb_builder_node_insert_text(info, "scope",
                                as_component_scope_to_string(AS_COMPONENT_SCOPE_USER), NULL);
    xb_builder_source_set_info(source, info);

    // Import source to builder
    xb_builder_import_source(builder, source);

    // Save to silo
    g_mutex_lock(&self->silo_mutex);
    self->silo = xb_builder_ensure(builder, silo_file,
                                   XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID |
                                       XB_BUILDER_COMPILE_FLAG_SINGLE_LANG,
                                   cancellable, &error);

    g_mutex_unlock(&self->silo_mutex);
    if (self->silo == NULL) {
        g_debug("Failed to create silo: %s", error->message);
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    g_task_return_boolean(task, TRUE);
}

static gboolean
refresh_plugin_cache(GsPluginVanillaMeta *self, GCancellable *cancellable, GError **error)
{
    g_autofree gchar *xpath         = NULL;
    g_autoptr(GPtrArray) components = NULL;
    g_autoptr(GError) local_error   = NULL;
    g_autoptr(GsApp) app            = NULL;
    GsPluginRefineFlags refine_flags;

    g_mutex_lock(&self->silo_mutex);
    xpath      = g_strdup_printf("components[@origin='vanilla_meta']/component/id");
    components = xb_silo_query(self->silo, xpath, 0, &local_error);
    g_mutex_unlock(&self->silo_mutex);

    if (local_error != NULL) {
        g_debug("Plugin cache query returned error: %s", local_error->message);
        return FALSE;
    }

    for (guint i = 0; i < components->len; i++) {
        XbNode *node = components->pdata[i];
        g_debug("Ensure: %s", xb_node_get_text(node));

        app = gs_plugin_cache_lookup(GS_PLUGIN(self), xb_node_get_text(node));
        if (app == NULL) {
            app = gs_app_new(xb_node_get_text(node));
            gs_app_set_management_plugin(app, GS_PLUGIN(self));
            gs_app_set_origin(app, "vanilla_meta");

            gs_plugin_cache_add(GS_PLUGIN(self), xb_node_get_text(node), app);

            refine_flags = GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
                           GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE | GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID;
            if (!refine_app(self, app, refine_flags, cancellable, &local_error)) {
                g_debug("Could not refine app %s", gs_app_get_id(app));
                return FALSE;
            }
        }
    }

    return TRUE;
}

static gboolean
gs_plugin_vanilla_meta_setup_finish(GsPlugin *plugin, GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void
gs_plugin_vanilla_meta_init(GsPluginVanillaMeta *self)
{
    GsPlugin *plugin = GS_PLUGIN(self);

    gs_plugin_set_appstream_id(plugin, "org.gnome.Software.Plugin.VanillaMeta");

    gs_plugin_add_rule(plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
    gs_plugin_add_rule(plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");
}

gboolean
gs_plugin_add_sources(GsPlugin *plugin, GsAppList *list, GCancellable *cancellable, GError **error)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);
    g_autoptr(GsApp) app      = NULL;

    g_debug("Adding sources");

    // Create source
    /* app = gs_app_new("org.vanillaos.vanilla-meta"); */
    app = gs_app_new("vanilla_meta");
    gs_app_set_kind(app, AS_COMPONENT_KIND_REPOSITORY);
    gs_app_set_state(app, GS_APP_STATE_INSTALLED);
    gs_app_add_quirk(app, GS_APP_QUIRK_NOT_LAUNCHABLE);
    gs_app_set_size_download(app, GS_SIZE_TYPE_UNKNOWABLE, 0);
    gs_app_set_management_plugin(app, plugin);
    gs_vanilla_meta_app_set_packaging_info(app);
    gs_app_set_scope(app, AS_COMPONENT_SCOPE_USER);
    gs_app_set_branch(app, "main");

    gs_app_set_metadata(app, "GnomeSoftware::SortKey", "200");
    gs_app_set_metadata(app, "GnomeSoftware::InstallationKind", "User Installation");
    gs_app_add_quirk(app, GS_APP_QUIRK_PROVENANCE);

    gs_app_set_name(app, GS_APP_QUALITY_NORMAL, "VanillaOS Meta");
    gs_app_set_summary(app, GS_APP_QUALITY_NORMAL,
                       _("Applications installable via Apx with pre-defined container configuration"));

    /* origin_ui on a remote is the repo dialogue section name,
     * not the remote title */
    gs_app_set_origin_ui(app, _("Apps"));
    gs_app_set_description(app, GS_APP_QUALITY_NORMAL,
                           _("This repository contains a set of popular applications installable via Apx and pre-configured by the Vanilla OS team to guarantee that they are using the most compatible container and configurations."));
    gs_app_set_url(app, AS_URL_KIND_HOMEPAGE, "https://vanillaos.org");

    gs_app_list_add(list, app);

    // Add related apps (the ones installed from our repo)
    g_mutex_lock(&self->silo_mutex);
    if (self->silo != NULL) {
        g_autofree gchar *xpath         = NULL;
        g_autoptr(GPtrArray) components = NULL;
        g_autoptr(GError) local_error   = NULL;

        xpath      = g_strdup_printf("components[@origin='vanilla_meta']/component");
        components = xb_silo_query(self->silo, xpath, 0, &local_error);
        if (local_error != NULL) {
            g_debug("Failed to add sources");
            g_mutex_unlock(&self->silo_mutex);
            return FALSE;
        }

        for (guint i = 0; i < components->len; i++) {
            g_autoptr(GsApp) related =
                gs_appstream_create_app(plugin, self->silo, components->pdata[i], &local_error);

            g_debug("Created app %s", gs_app_get_name(related));
            if (check_app_is_installed(related, cancellable, local_error, TRUE))
                gs_app_add_related(app, related);
        }
    } else {
        g_debug("Silo is not initialized");
    }
    g_mutex_unlock(&self->silo_mutex);

    return TRUE;
}

static void
gs_plugin_vanilla_meta_enable_repository_async(GsPlugin *plugin,
                                               GsApp *repository,
                                               GsPluginManageRepositoryFlags flags,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);
    g_autoptr(GTask) task     = NULL;
    gboolean interactive      = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

    task = gs_plugin_manage_repository_data_new_task(plugin, repository, flags, cancellable,
                                                     callback, user_data);
    g_task_set_source_tag(task, gs_plugin_vanilla_meta_enable_repository_async);

    /* only process this app if was created by this plugin */
    if (!gs_app_has_management_plugin(repository, plugin)) {
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* is a source */
    g_assert(gs_app_get_kind(repository) == AS_COMPONENT_KIND_REPOSITORY);

    gs_worker_thread_queue(self->worker, get_priority_for_interactivity(interactive),
                           enable_repository_thread_cb, g_steal_pointer(&task));
}

static void
enable_repository_thread_cb(GTask *task,
                            gpointer source_object,
                            gpointer task_data,
                            GCancellable *cancellable)
{
    GsPluginVanillaMeta *self          = GS_PLUGIN_VANILLA_META(source_object);
    GsPluginManageRepositoryData *data = task_data;

    assert_in_worker(self);

    gs_app_set_state(data->repository, GS_APP_STATE_INSTALLED);
    gs_plugin_repository_changed(GS_PLUGIN(self), data->repository);

    g_task_return_boolean(task, TRUE);
}

static gboolean
gs_plugin_vanilla_meta_enable_repository_finish(GsPlugin *plugin,
                                                GAsyncResult *result,
                                                GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void
gs_plugin_vanilla_meta_disable_repository_async(GsPlugin *plugin,
                                                GsApp *repository,
                                                GsPluginManageRepositoryFlags flags,
                                                GCancellable *cancellable,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);

    /* only process this app if was created by this plugin */
    if (!gs_app_has_management_plugin(repository, plugin)) {
        g_autoptr(GTask) task = NULL;

        task = g_task_new(self, cancellable, callback, user_data);
        g_task_set_source_tag(task, gs_plugin_vanilla_meta_disable_repository_async);
        g_task_return_boolean(task, TRUE);
        return;
    }

    gs_app_set_state(repository, GS_APP_STATE_AVAILABLE);
    gs_plugin_repository_changed(GS_PLUGIN(self), repository);
}

static gboolean
gs_plugin_vanilla_meta_disable_repository_finish(GsPlugin *plugin,
                                                 GAsyncResult *result,
                                                 GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

static gboolean
plugin_vanillameta_pick_apx_desktop_file_cb(GsPlugin *plugin,
                                            GsApp *app,
                                            const gchar *filename,
                                            GKeyFile *key_file)
{
    return strstr(filename, "/snapd/") == NULL && strstr(filename, "/snap/") == NULL &&
           strstr(filename, "/flatpak/") == NULL &&
           g_key_file_has_group(key_file, "Desktop Entry") &&
           !g_key_file_has_key(key_file, "Desktop Entry", "X-Flatpak", NULL) &&
           !g_key_file_has_key(key_file, "Desktop Entry", "X-SnapInstanceName", NULL);
}

gboolean
gs_plugin_launch(GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
    /* only process this app if was created by this plugin */
    if (!gs_app_has_management_plugin(app, plugin))
        return TRUE;

    return gs_plugin_app_launch_filtered(plugin, app, plugin_vanillameta_pick_apx_desktop_file_cb,
                                         NULL, error);
}

gboolean
gs_plugin_app_install(GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
    g_autofree const gchar *container_flag = NULL;
    g_autofree const gchar *install_cmd    = NULL;
    const gchar *app_container_name        = gs_app_get_metadata_item(app, "Vanilla::container");
    const gchar *package_name              = NULL;
    SubprocessOutput *output               = NULL;

    // Only process this app if was created by this plugin
    if (!gs_app_has_management_plugin(app, plugin))
        return TRUE;

    // Check container exists, otherwise run init for it
    output = gs_vanilla_meta_run_subprocess(
        "podman container ls --noheading -a | rev | cut -d\' \' -f 1 | rev",
        G_SUBPROCESS_FLAGS_STDOUT_PIPE, cancellable, error);

    if (output->input_stream != NULL) {
        g_autoptr(GByteArray) ls_out = g_byte_array_new();
        gchar buffer[4096];
        gsize nread = 0;
        gboolean success;
        g_auto(GStrv) splits = NULL;

        gs_app_set_state(app, GS_APP_STATE_INSTALLING);

        while (success = g_input_stream_read_all(output->input_stream, buffer, sizeof(buffer),
                                                 &nread, cancellable, error),
               success && nread > 0) {
            g_byte_array_append(ls_out, (const guint8 *)buffer, nread);
        }

        // If we have a valid output
        if (success && ls_out->len > 0) {
            gboolean container_installed = FALSE;
            // NUL-terminate the array, to use it as a string
            g_byte_array_append(ls_out, (const guint8 *)"", 1);

            splits = g_strsplit((gchar *)ls_out->data, "\n", -1);

            if (app_container_name == NULL) {
                g_debug("Install: Container name not set for %s, cannot install",
                        gs_app_get_name(app));
                gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
                free(output);
                return FALSE;
            }

            for (guint i = 0; i < g_strv_length(splits); i++) {
                if (g_strcmp0(splits[i], app_container_name) == 0) {
                    // Container is installed, nothing to do for now
                    g_debug("Container %s already initialized", app_container_name);
                    container_installed = TRUE;
                    break;
                }
            }

            // Initialize container
            if (!container_installed) {
                g_autofree const gchar *init_cmd = g_strdup_printf("apx %s init", container_flag);
                g_debug("Install: Running init for container %s", app_container_name);

                container_flag = apx_container_flag_from_name(app_container_name);

                gs_vanilla_meta_run_subprocess(init_cmd, G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
                                               cancellable, error);
            }
        }
    }
    free(output);

    // Install package and process output
    if (container_flag == NULL)
        container_flag = apx_container_flag_from_name(app_container_name);

    package_name = gs_app_get_source_default(app);
    if (package_name == NULL) {
        g_debug("Install: Package name for %s is null, can't install", gs_app_get_name(app));
        gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
        return FALSE;
    }

    g_debug("Installing app %s, using container flag `%s` and package name `%s`",
            gs_app_get_name(app), container_flag, package_name);

    install_cmd = g_strdup_printf("apx %s install -y %s", container_flag, package_name);

    output = gs_vanilla_meta_run_subprocess(install_cmd, G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
                                            cancellable, error);
    if (output->input_stream != NULL) {
        gs_app_set_state(app, GS_APP_STATE_INSTALLED);
        free(output);
        return TRUE;
    } else {
        gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
        free(output);
        return FALSE;
    }
}

gboolean
gs_plugin_app_remove(GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
    g_autofree const gchar *container_flag = NULL;
    g_autofree const gchar *remove_cmd     = NULL;
    SubprocessOutput *output               = NULL;
    const gchar *package_name              = NULL;
    const gchar *app_container_name        = gs_app_get_metadata_item(app, "Vanilla::container");

    // Only process this app if was created by this plugin
    if (!gs_app_has_management_plugin(app, plugin))
        return TRUE;

    container_flag = apx_container_flag_from_name(app_container_name);
    package_name   = gs_app_get_source_default(app);
    if (package_name == NULL) {
        g_debug("Remove: Package name for %s is null, can't remove", gs_app_get_name(app));
        gs_app_set_state(app, GS_APP_STATE_UNKNOWN); // We have no idea if remove actually ran
        return FALSE;
    }

    remove_cmd = g_strdup_printf("apx %s remove -y %s", container_flag, package_name);

    output = gs_vanilla_meta_run_subprocess(remove_cmd, G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
                                            cancellable, error);

    if (output->input_stream != NULL) {
        gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
        free(output);
        return TRUE;
    } else {
        gs_app_set_state(app, GS_APP_STATE_UNKNOWN);
        free(output);
        return FALSE;
    }
}

void
gs_plugin_adopt_app(GsPlugin *plugin, GsApp *app)
{
    if (!g_strcmp0(gs_app_get_origin(app), "vanilla_meta")) {
        g_debug("I should adopt app %s", gs_app_get_name(app));
        gs_app_set_management_plugin(app, plugin);
        gs_app_set_scope(app, AS_COMPONENT_SCOPE_USER);
        gs_app_add_quirk(app, GS_APP_QUIRK_PROVENANCE);
        gs_vanilla_meta_app_set_packaging_info(app);

        if (gs_plugin_cache_lookup(plugin, gs_app_get_id(app)) == NULL) {
            gs_plugin_cache_add(plugin, gs_app_get_id(app), app);
        }
    }
}

gboolean
check_app_is_installed(GsApp *app, GCancellable *cancellable, GError *error, gboolean update_status)
{
    const gchar *package_name       = NULL;
    const gchar *container_flag     = NULL;
    const gchar *check_cmd          = NULL;
    const gchar *app_container_name = NULL;
    SubprocessOutput *output        = NULL;
    gboolean query_result           = FALSE;

    app_container_name = gs_app_get_metadata_item(app, "Vanilla::container");
    container_flag = apx_container_flag_from_name(app_container_name);
    package_name   = gs_app_get_source_default(app);
    if (package_name == NULL) {
        g_debug("Check installed: Package name for %s is null, can't verify", gs_app_get_name(app));
        gs_app_set_state(app, GS_APP_STATE_UNKNOWN);
        return FALSE;
    }

    check_cmd = g_strdup_printf("apx %s show -i %s", container_flag, package_name);

    output = gs_vanilla_meta_run_subprocess(
        check_cmd, G_SUBPROCESS_FLAGS_STDOUT_SILENCE, cancellable, &error);

    if (output != NULL) {
        if (output->exit_code == EXIT_SUCCESS) {
            g_debug("Package %s is installed", gs_app_get_name(app));
            if (update_status)
                gs_app_set_state(app, GS_APP_STATE_INSTALLED);
            query_result =  TRUE;
        } else {
            g_debug("Package %s is not installed", gs_app_get_name(app));
            if (update_status)
                gs_app_set_state(app, GS_APP_STATE_AVAILABLE);
            query_result = FALSE;
        }
    }

    free(output);
    return query_result;
}

static void
gs_plugin_vanilla_meta_list_apps_async(GsPlugin *plugin,
                                       GsAppQuery *query,
                                       GsPluginListAppsFlags flags,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);
    g_autoptr(GTask) task     = NULL;
    gboolean interactive      = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

    task =
        gs_plugin_list_apps_data_new_task(plugin, query, flags, cancellable, callback, user_data);
    g_task_set_source_tag(task, gs_plugin_vanilla_meta_list_apps_async);

    /* Queue a job to get the apps. */
    gs_worker_thread_queue(self->worker, get_priority_for_interactivity(interactive),
                           list_apps_thread_cb, g_steal_pointer(&task));
}

static void
list_apps_thread_cb(GTask *task,
                    gpointer source_object,
                    gpointer task_data,
                    GCancellable *cancellable)
{
    GsPluginVanillaMeta *self       = GS_PLUGIN_VANILLA_META(source_object);
    g_autoptr(GsAppList) list       = gs_app_list_new();
    GsPluginListAppsData *data      = task_data;
    GsAppQueryTristate is_installed = GS_APP_QUERY_TRISTATE_UNSET;
    GsCategory *category            = NULL;
    GsApp *alternate_of             = NULL;
    g_autoptr(GError) local_error   = NULL;

    assert_in_worker(self);

    refresh_plugin_cache(self, cancellable, &local_error);

    if (data->query != NULL) {
        category     = gs_app_query_get_category(data->query);
        is_installed = gs_app_query_get_is_installed(data->query);
        alternate_of = gs_app_query_get_alternate_of(data->query);
    }

    /* Currently only support a subset of query properties, and only one set at once. */
    if ((is_installed == GS_APP_QUERY_TRISTATE_FALSE && alternate_of == NULL) ||
        gs_app_query_get_n_properties_set(data->query) != 1) {
        g_debug("Unsupported query");
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unsupported query");
        return;
    }

    if (is_installed == GS_APP_QUERY_TRISTATE_TRUE) {
        GsAppList *installed_apps = gs_app_list_new();

        gs_plugin_cache_lookup_by_state(GS_PLUGIN(self), installed_apps, GS_APP_STATE_INSTALLED);
        gs_app_list_add_list(list, installed_apps);
    }

    if (category != NULL) {
        g_autoptr(GsAppList) list_tmp = gs_app_list_new();

        if (!gs_appstream_add_category_apps(GS_PLUGIN(self), self->silo, category, list_tmp,
                                            cancellable, &local_error)) {
            g_task_return_error(task, local_error);
            return;
        }

        for (guint i = 0; i < gs_app_list_length(list_tmp); i++) {
            GsApp *app        = gs_app_list_index(list_tmp, i);
            GsApp *cached_app = gs_plugin_cache_lookup(GS_PLUGIN(self), gs_app_get_id(app));

            if (cached_app != NULL) {
                g_debug("category: Adding app %s", gs_app_get_name(cached_app));
                gs_app_list_add(list, cached_app);
            }
        }
    }

    if (alternate_of != NULL) {
        GsApp *app = gs_plugin_cache_lookup(GS_PLUGIN(self), gs_app_get_id(alternate_of));

        if (app != NULL)
            gs_app_list_add(list, app);
    }

    g_task_return_pointer(task, g_steal_pointer(&list), g_object_unref);
}

static GsAppList *
gs_plugin_vanilla_meta_list_apps_finish(GsPlugin *plugin, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(
        g_task_get_source_tag(G_TASK(result)) == gs_plugin_vanilla_meta_list_apps_async, FALSE);
    return g_task_propagate_pointer(G_TASK(result), error);
}

static void
gs_plugin_vanilla_meta_refine_async(GsPlugin *plugin,
                                    GsAppList *list,
                                    GsPluginRefineFlags flags,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    GsPluginVanillaMeta *self = GS_PLUGIN_VANILLA_META(plugin);
    g_autoptr(GTask) task     = NULL;
    gboolean interactive      = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

    task = gs_plugin_refine_data_new_task(plugin, list, flags, cancellable, callback, user_data);

    g_task_set_source_tag(task, gs_plugin_vanilla_meta_refine_async);

    gs_worker_thread_queue(self->worker, get_priority_for_interactivity(interactive),
                           refine_thread_cb, g_steal_pointer(&task));
}

static void
refine_thread_cb(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    GsPluginVanillaMeta *self     = GS_PLUGIN_VANILLA_META(source_object);
    GsPluginRefineData *data      = task_data;
    g_autoptr(GError) local_error = NULL;

    assert_in_worker(self);

    refresh_plugin_cache(self, cancellable, &local_error);

    for (guint i = 0; i < gs_app_list_length(data->list); i++) {
        GsApp *app               = gs_app_list_index(data->list, i);

        if (g_strcmp0(gs_app_get_origin(app), "vanilla_meta"))
            continue;

        if (!refine_app(self, app, data->flags, cancellable, &local_error)) {
            g_task_return_error(task, g_steal_pointer(&local_error));
        }
    }

    g_task_return_boolean(task, TRUE);
}

static gboolean
refine_app(GsPluginVanillaMeta *self,
           GsApp *app,
           GsPluginRefineFlags flags,
           GCancellable *cancellable,
           GError **error)
{
    g_autofree gchar *id_safe     = NULL;
    g_autofree gchar *xpath       = NULL;
    g_autoptr(XbNode) component   = NULL;
    const gchar *container_name   = NULL;
    g_autoptr(XbNode) child       = NULL;
    g_autoptr(GError) local_error = NULL;
    XbNodeChildIter iter;

    if (!gs_app_has_management_plugin(app, NULL))
        gs_app_set_management_plugin(app, GS_PLUGIN(self));

    if (gs_app_has_quirk(app, GS_APP_QUIRK_IS_WILDCARD)) {
        g_debug("App %s is wildcard. Skipping..", gs_app_get_id(app));
        return FALSE;
    }

    /* find using source and origin */
    id_safe = xb_string_escape(gs_app_get_id(app));
    xpath   = g_strdup_printf("components[@origin='vanilla_meta']/component/"
                                "id[text()='%s']/..",
                              id_safe);

    g_mutex_lock(&self->silo_mutex);
    component = xb_silo_query_first(self->silo, xpath, &local_error);
    if (local_error != NULL) {
        g_debug("Failed to refine app %s in query stage", gs_app_get_name(app));
        return FALSE;
    }

    // TODO: Find a way to query file sizes
    /* if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) { */
    /*     gs_app_set_size_download(app, GS_SIZE_TYPE_VALID, 0); */
    /*     gs_app_set_size_cache_data(app, GS_SIZE_TYPE_VALID, 0); */
    /*     gs_app_set_size_user_data(app, GS_SIZE_TYPE_VALID, 0); */
    /*     gs_app_set_size_installed(app, GS_SIZE_TYPE_VALID, 0); */
    /* } */

    gs_app_add_quirk(app, GS_APP_QUIRK_PROVENANCE);

    gs_app_set_origin(app, "vanilla_meta");
    gs_app_set_origin_ui(app, "VanillaOS Meta");
    gs_app_set_origin_hostname(app, "https://vanillaos.org");

    if (component == NULL) {
        g_debug("no match for %s: %s", xpath, local_error->message);
        g_clear_error(&local_error);
        return FALSE;
    }

    gs_appstream_refine_app(GS_PLUGIN(self), app, self->silo, component, flags, &local_error);
    if (local_error != NULL) {
        g_debug("Failed to refine app %s", gs_app_get_name(app));
        return FALSE;
    }
    g_mutex_unlock(&self->silo_mutex);

    check_app_is_installed(app, cancellable, local_error, TRUE);

    // Iterate node's children until we find container name
    xb_node_child_iter_init(&iter, component);
    while (xb_node_child_iter_next(&iter, &child)) {
        container_name = xb_node_get_attr(child, "container");
        if (container_name != NULL)
            break;
    }

    gs_app_set_metadata(app, "Vanilla::container", container_name);
    g_debug("Adding container %s to app %s", container_name, gs_app_get_name(app));

    gs_app_set_metadata(app, "GnomeSoftware::PackagingFormat",
                        apx_container_name_to_alias(container_name));

    gs_vanilla_meta_app_set_packaging_info(app);

    g_debug("Refined %s", gs_app_get_id(app));
    return TRUE;
}

static gboolean
gs_plugin_vanilla_meta_refine_finish(GsPlugin *plugin, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(
        g_task_get_source_tag(G_TASK(result)) == gs_plugin_vanilla_meta_refine_async, FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void
gs_plugin_vanilla_meta_class_init(GsPluginVanillaMetaClass *klass)
{
    GObjectClass *object_class  = G_OBJECT_CLASS(klass);
    GsPluginClass *plugin_class = GS_PLUGIN_CLASS(klass);

    object_class->dispose  = gs_plugin_vanilla_meta_dispose;
    object_class->finalize = gs_plugin_vanilla_meta_finalize;

    plugin_class->setup_async               = gs_plugin_vanilla_meta_setup_async;
    plugin_class->setup_finish              = gs_plugin_vanilla_meta_setup_finish;
    plugin_class->enable_repository_async   = gs_plugin_vanilla_meta_enable_repository_async;
    plugin_class->enable_repository_finish  = gs_plugin_vanilla_meta_enable_repository_finish;
    plugin_class->disable_repository_async  = gs_plugin_vanilla_meta_disable_repository_async;
    plugin_class->disable_repository_finish = gs_plugin_vanilla_meta_disable_repository_finish;
    plugin_class->list_apps_async           = gs_plugin_vanilla_meta_list_apps_async;
    plugin_class->list_apps_finish          = gs_plugin_vanilla_meta_list_apps_finish;
    plugin_class->refine_async              = gs_plugin_vanilla_meta_refine_async;
    plugin_class->refine_finish             = gs_plugin_vanilla_meta_refine_finish;
}

GType
gs_plugin_query_type(void)
{
    return GS_TYPE_PLUGIN_VANILLA_META;
}
