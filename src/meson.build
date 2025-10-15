src_inc = include_directories('.')

common_sources = files(
  'common.c',
  'music.c',
  'mod_mp3.c',
  'mod_ogg.c',
  'mod_flac.c',
  'mod_pls.c',
)

mp3berg_sources = common_sources + [
  'mp3berg.c',
  git_version,
]

mp3build_sources = common_sources + [
  'mp3build.c',
  git_version,
]

mp3berg_deps = [
  ncurses_dep,
  threads_dep,
  vorbisfile_dep,
  vorbis_dep,
  flac_dep,
  ogg_dep,
]
mp3berg_deps += mp3berg_extra_deps

mp3build_deps = [
  threads_dep,
  vorbisfile_dep,
  vorbis_dep,
  flac_dep,
  ogg_dep,
]

if m_dep.found()
  mp3berg_deps += m_dep
  mp3build_deps += m_dep
endif

executable(
  'mp3berg',
  mp3berg_sources,
  include_directories: src_inc,
  dependencies: mp3berg_deps,
  c_args: mp3berg_c_args,
  install: true,
)

executable(
  'mp3build',
  mp3build_sources,
  include_directories: src_inc,
  dependencies: mp3build_deps,
  install: true,
)
