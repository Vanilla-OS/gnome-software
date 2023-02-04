cargs = ['-DG_LOG_DOMAIN="GsPluginVSO"']

files = [
  'gs-plugin-vso.c',
]

shared_module(
  'gs_plugin_vso',
  sources : files,
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, polkit ],
)
