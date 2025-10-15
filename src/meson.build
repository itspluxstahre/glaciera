src_inc = include_directories('.')

common_sources = files(
  'common.c',
  'config.c',
  'toml.c',
  'db.c',
  'music.c',
  'mod_mp3.c',
  'mod_ogg.c',
  'mod_flac.c',
  'mod_pls.c',
)

glaciera_sources = common_sources + [
  'glaciera.c',
  git_version,
]

glaciera_indexer_sources = common_sources + [
  'glaciera-indexer.c',
  git_version,
]

glaciera_deps = [
  ncurses_dep,
  threads_dep,
  sqlite3_dep,
  vorbisfile_dep,
  vorbis_dep,
  flac_dep,
  ogg_dep,
]
glaciera_deps += glaciera_extra_deps

glaciera_indexer_deps = [
  threads_dep,
  sqlite3_dep,
  vorbisfile_dep,
  vorbis_dep,
  flac_dep,
  ogg_dep,
]

if m_dep.found()
  glaciera_deps += m_dep
  glaciera_indexer_deps += m_dep
endif

executable(
  'glaciera',
  glaciera_sources,
  include_directories: src_inc,
  dependencies: glaciera_deps,
  c_args: glaciera_c_args,
  link_args: static_binaries ? ['-static'] : [],
  install: true,
)

executable(
  'glaciera-indexer',
  glaciera_indexer_sources,
  include_directories: src_inc,
  dependencies: glaciera_indexer_deps,
  link_args: static_binaries ? ['-static'] : [],
  install: true,
)
