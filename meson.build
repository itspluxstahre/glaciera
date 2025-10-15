project(
  'mp3berg',
  'c',
  version: '4.0',
  default_options: [
    'c_std=c2x',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

ncurses_dep = dependency('ncursesw', required: false)
if not ncurses_dep.found()
  ncurses_dep = dependency('ncurses', required: true)
endif

threads_dep = dependency('threads')
vorbisfile_dep = dependency('vorbisfile')
vorbis_dep = dependency('vorbis')
flac_dep = dependency('flac')
ogg_dep = dependency('ogg')
m_dep = cc.find_library('m', required: false)

libintl_dep = dependency('intl', required: false)
have_libintl_header = cc.has_header('libintl.h')

mp3berg_extra_deps = []
mp3berg_c_args = []

if libintl_dep.found()
  mp3berg_extra_deps += libintl_dep
endif

if have_libintl_header
  mp3berg_c_args += ['-DHAVE_LIBINTL_H']
endif

git_version_script = find_program('generate_git_version_c.sh')
git_version = custom_target(
  'git_version',
  output: 'git_version.c',
  command: [git_version_script, '@OUTPUT@'],
  build_by_default: true,
  build_always_stale: true,
)

subdir('src')
