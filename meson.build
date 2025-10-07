project('patchutils', 'c', version: '0.1.0', default_options: ['warning_level=3', 'c_std=gnu2x'])

dep_ncurses = dependency('ncursesw', required: true)
dep_libgit2 = dependency('libgit2', required: true)

cc = meson.get_compiler('c')
if cc.get_id() != 'clang'
  error('Patchutils requires clang. Configure with CC=clang meson setup …')
endif

subdir('src')
