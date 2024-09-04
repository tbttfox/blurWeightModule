skin_brush_files = files([
  'src/functions.cpp',
  'src/pluginMain.cpp',
  'src/skinBrushCmd.cpp',
  'src/skinBrushContext.cpp',
  'src/skinBrushContextSetFlags.cpp',
  'src/skinBrushLegacy.cpp',
  'src/skinBrushTool.cpp',
])

gl_dep = dependency('gl')

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
  skin_brush_files = skin_brush_files + version_h
endif

skin_brush_inc = include_directories(['include'])

skin_brush_lib = shared_library(
  'brSkinBrush',
  skin_brush_files,
  install: true,
  install_dir : meson.global_source_root() / 'output_Maya' + maya_version,
  include_directories : skin_brush_inc,
  dependencies : [maya_dep, gl_dep],
  name_prefix : '',
  name_suffix : maya_name_suffix,
)
