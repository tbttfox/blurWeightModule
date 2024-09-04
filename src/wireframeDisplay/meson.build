wire_disp_files = files([
  'src/pluginMain.cpp',
  'src/wireframeDisplay.cpp',
])

if fs.is_file('src/version.h')
  message('Using existing version.h')
else
  git = find_program('git', native: true, required: true)
  version_h = vcs_tag(
    command: [git, 'describe', '--tags', '--match', 'v[0-9]*', '--dirty=+'],
    fallback: 'v0.0.1',
    input: 'src/version.h.in',
    output: 'version.h',
  )
  wire_disp_files = wire_disp_files + version_h
endif

wire_disp_inc = include_directories(['include'])

wire_disp_lib = shared_library(
  'wireframeDisplay',
  wire_disp_files,
  install: true,
  install_dir : meson.global_source_root() / 'output_Maya' + maya_version,
  include_directories : wire_disp_inc,
  dependencies : [maya_dep],
  name_prefix : '',
  name_suffix : maya_name_suffix,
)
