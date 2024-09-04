blur_skin_files = files([
  'src/blurSkinCmd.cpp',
  'src/blurSkinEdit.cpp',
  'src/functions.cpp',
  'src/pluginMain.cpp',
  'src/pointsDisplay.cpp',
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
  blur_skin_files = blur_skin_files + version_h
endif

blur_skin_inc = include_directories(['include'])

blur_skin_lib = shared_library(
  'blurSkin',
  blur_skin_files,
  install: true,
  install_dir : meson.global_source_root() / 'output_Maya' + maya_version,
  include_directories : blur_skin_inc,
  dependencies : [maya_dep],
  name_prefix : '',
  name_suffix : maya_name_suffix,
)
