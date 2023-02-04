cargs = ['-DG_LOG_DOMAIN="GsPluginVanillaMeta"']

files = [
  'gs-plugin-vanilla-meta.c',
  'gs-vanilla-meta-util.c'
]

shared_module(
  'gs_plugin_vanilla_meta',
  sources : files,
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, appstream ],
)
