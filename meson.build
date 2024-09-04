project('blurWeightModule', 'cpp', default_options: ['cpp_std=c++20'])

fs = import('fs')
maya_dep = dependency('maya')
maya_name_suffix = maya_dep.get_variable('name_suffix')
maya_version = maya_dep.get_variable('maya_version')

subdir('src/blurSkin')
subdir('src/brSkinBrush')
subdir('src/wireframeDisplay')
