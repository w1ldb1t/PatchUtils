src_inc   = include_directories('.')
libs_inc = include_directories('libs')

common_lib = static_library(
  'patchutils_libs',
  files(
    'libs/git/git.c',
    'libs/ui/ui.c',
    'libs/util/util.c',
    'libs/patch/patch.c',
  ),
  include_directories: [src_inc, libs_inc],
  install: false,
)

common_deps = [dep_ncurses, dep_libgit2]

executable(
  'create-patch',
  'apps/create_patch.c',
  link_with: common_lib,
  dependencies: common_deps,
  include_directories: [src_inc, libs_inc],
  install: true,
)

executable(
  'split-patch',
  'apps/split_patch.c',
  link_with: common_lib,
  dependencies: common_deps,
  include_directories: [src_inc, libs_inc],
  install: true,
)

executable(
  'update-patch',
  'apps/update_patch.c',
  link_with: common_lib,
  dependencies: common_deps,
  include_directories: [src_inc, libs_inc],
  install: true,
)
