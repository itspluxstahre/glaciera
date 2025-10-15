project(
  'glaciera',
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
sqlite3_dep = dependency('sqlite3')
vorbisfile_dep = dependency('vorbisfile')
vorbis_dep = dependency('vorbis')
flac_dep = dependency('flac')
ogg_dep = dependency('ogg')
m_dep = cc.find_library('m', required: false)

libintl_dep = dependency('intl', required: false)
have_libintl_header = cc.has_header('libintl.h')

glaciera_extra_deps = []
glaciera_c_args = []

if libintl_dep.found()
  glaciera_extra_deps += libintl_dep
endif

if have_libintl_header
  glaciera_c_args += ['-DHAVE_LIBINTL_H']
endif

git_exe = find_program('git', required: false)

git_description = 'unknown'
if git_exe.found()
  git_res = run_command(git_exe, 'describe', '--tags', '--dirty', '--always', check: false)
  if git_res.returncode() == 0
    git_description = git_res.stdout().strip()
  endif
  if git_description == '' or git_res.returncode() != 0
    git_res = run_command(git_exe, 'rev-parse', '--short', 'HEAD', check: false)
    if git_res.returncode() == 0
      git_description = git_res.stdout().strip()
    endif
  endif
endif

# Create complete version string like "Version 4.0-d2599dc"
complete_version = 'Version @0@-@1@'.format(meson.project_version(), git_description)

git_desc_escaped = git_description.replace('\\', '\\\\').replace('"', '\\"')
complete_version_escaped = complete_version.replace('\\', '\\\\').replace('"', '\\"')

git_version_conf = configuration_data()
git_version_conf.set('GIT_DESCRIPTION', '"@0@"'.format(git_desc_escaped))
git_version_conf.set('COMPLETE_VERSION', '"@0@"'.format(complete_version_escaped))

git_version = configure_file(
  output: 'git_version.c',
  input: files('meson/git_version.c.in'),
  configuration: git_version_conf,
  install: false,
)

subdir('src')
