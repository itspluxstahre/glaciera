/*
 * config.c - Modern XDG-compliant configuration system for Glaciera
 *
 * Copyright (c) 2025 Glaciera Contributors
 */

// System headers
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Local headers
#include "config.h"
#include "toml.h"

char xdg_config_dir[512];
char xdg_data_dir[512];
char xdg_cache_dir[512];
config_t global_config;

static char db_path_cache[512];

/* Create directory if it doesn't exist */
static bool ensure_dir(const char *path) {
	struct stat st;
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);
	}

	if (mkdir(path, 0755) == 0) {
		return true;
	}

	if (errno == EEXIST) {
		return true;
	}

	fprintf(stderr, "Error: Failed to create directory %s: %s\n", path, strerror(errno));
	return false;
}

/* Initialize XDG directories */
static bool init_xdg_dirs(void) {
	const char *home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "Error: HOME environment variable not set\n");
		return false;
	}

	/* Build paths directly to avoid static buffer issues */
	const char *config_env = getenv("XDG_CONFIG_HOME");
	const char *data_env = getenv("XDG_DATA_HOME");
	const char *cache_env = getenv("XDG_CACHE_HOME");

	if (config_env && config_env[0] != '\0') {
		snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/glaciera", config_env);
	} else {
		snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/.config/glaciera", home);
	}

	if (data_env && data_env[0] != '\0') {
		snprintf(xdg_data_dir, sizeof(xdg_data_dir), "%s/glaciera", data_env);
	} else {
		snprintf(xdg_data_dir, sizeof(xdg_data_dir), "%s/.local/share/glaciera", home);
	}

	if (cache_env && cache_env[0] != '\0') {
		snprintf(xdg_cache_dir, sizeof(xdg_cache_dir), "%s/glaciera", cache_env);
	} else {
		snprintf(xdg_cache_dir, sizeof(xdg_cache_dir), "%s/.cache/glaciera", home);
	}

	/* Create directories */
	if (!ensure_dir(xdg_config_dir))
		return false;
	if (!ensure_dir(xdg_data_dir))
		return false;
	if (!ensure_dir(xdg_cache_dir))
		return false;

	/* Create themes subdirectory */
	char themes_dir[512];
	snprintf(themes_dir, sizeof(themes_dir), "%s/themes", xdg_config_dir);
	if (!ensure_dir(themes_dir))
		return false;

	return true;
}

/* Set default configuration values */
void config_set_defaults(config_t *config) {
	const char *home = getenv("HOME");

	/* Default to ~/Music as first index path */
	config->index_paths_count = 1;
	snprintf(config->index_paths[0], sizeof(config->index_paths[0]), "%s/Music",
		 home ? home : "/tmp");

	snprintf(config->rippers_path, sizeof(config->rippers_path), "%s/Music/rippers",
		 home ? home : "/tmp");

	strcpy(config->mp3_player_path, "mpg123");
	config->mp3_player_flags[0] = '\0';
	strcpy(config->ogg_player_path, "ogg123");
	config->ogg_player_flags[0] = '\0';
	strcpy(config->flac_player_path, "ogg123");
	config->flac_player_flags[0] = '\0';

	/* Default Nord dark theme */
	strcpy(config->theme_name, "default");
	strcpy(config->theme.name, "Nord Dark (Default)");

	/* Nord Polar Night (dark backgrounds) */
	config->theme.main_bg.r = 46;
	config->theme.main_bg.g = 52;
	config->theme.main_bg.b = 64; /* #2e3440 */
	config->theme.main_fg.r = 216;
	config->theme.main_fg.g = 222;
	config->theme.main_fg.b = 233; /* #d8dee9 */

	/* Nord Frost (blue accent) */
	config->theme.accent_bg.r = 94;
	config->theme.accent_bg.g = 129;
	config->theme.accent_bg.b = 172; /* #5e81ac */
	config->theme.accent_fg.r = 236;
	config->theme.accent_fg.g = 239;
	config->theme.accent_fg.b = 244; /* #eceff4 */

	/* Nord Aurora (yellow and green) */
	config->theme.playing.r = 235;
	config->theme.playing.g = 203;
	config->theme.playing.b = 139; /* #ebcb8b */
	config->theme.playlist.r = 163;
	config->theme.playlist.g = 190;
	config->theme.playlist.b = 140; /* #a3be8c */

	/* Highlight colors */
	config->theme.highlight_bg.r = 236;
	config->theme.highlight_bg.g = 239;
	config->theme.highlight_bg.b = 244; /* #eceff4 */
	config->theme.highlight_fg.r = 46;
	config->theme.highlight_fg.g = 52;
	config->theme.highlight_fg.b = 64; /* #2e3440 */
}

/* Parse RGB color from TOML table */
static bool parse_color(toml_table_t *table, const char *key, int *r, int *g, int *b) {
	toml_table_t *color = toml_table_in(table, key);
	if (!color)
		return false;

	toml_datum_t rd = toml_int_in(color, "r");
	toml_datum_t gd = toml_int_in(color, "g");
	toml_datum_t bd = toml_int_in(color, "b");

	if (!rd.ok || !gd.ok || !bd.ok)
		return false;

	*r = (int)rd.u.i;
	*g = (int)gd.u.i;
	*b = (int)bd.u.i;

	return true;
}

/* Load theme from TOML file */
bool config_load_theme(const char *theme_name) {
	char theme_path[512];
	snprintf(theme_path, sizeof(theme_path), "%s/themes/%s.toml", xdg_config_dir, theme_name);

	FILE *fp = fopen(theme_path, "r");
	if (!fp) {
		fprintf(stderr, "Warning: Could not load theme '%s', using default\n", theme_name);
		return false;
	}

	char errbuf[200];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf) {
		fprintf(stderr, "Error parsing theme file: %s\n", errbuf);
		return false;
	}

	/* Read theme name */
	toml_datum_t name = toml_string_in(conf, "name");
	if (name.ok) {
		strncpy(global_config.theme.name, name.u.s, sizeof(global_config.theme.name) - 1);
		free(name.u.s);
	}

	/* Parse colors */
	toml_table_t *colors = toml_table_in(conf, "colors");
	if (colors) {
		parse_color(colors, "main_bg", &global_config.theme.main_bg.r,
			    &global_config.theme.main_bg.g, &global_config.theme.main_bg.b);
		parse_color(colors, "main_fg", &global_config.theme.main_fg.r,
			    &global_config.theme.main_fg.g, &global_config.theme.main_fg.b);
		parse_color(colors, "accent_bg", &global_config.theme.accent_bg.r,
			    &global_config.theme.accent_bg.g, &global_config.theme.accent_bg.b);
		parse_color(colors, "accent_fg", &global_config.theme.accent_fg.r,
			    &global_config.theme.accent_fg.g, &global_config.theme.accent_fg.b);
		parse_color(colors, "playing", &global_config.theme.playing.r,
			    &global_config.theme.playing.g, &global_config.theme.playing.b);
		parse_color(colors, "playlist", &global_config.theme.playlist.r,
			    &global_config.theme.playlist.g, &global_config.theme.playlist.b);
		parse_color(colors, "highlight_bg", &global_config.theme.highlight_bg.r,
			    &global_config.theme.highlight_bg.g,
			    &global_config.theme.highlight_bg.b);
		parse_color(colors, "highlight_fg", &global_config.theme.highlight_fg.r,
			    &global_config.theme.highlight_fg.g,
			    &global_config.theme.highlight_fg.b);
	}

	toml_free(conf);
	strncpy(global_config.theme_name, theme_name, sizeof(global_config.theme_name) - 1);
	return true;
}

/* Create default config.toml file */
bool config_create_default_file(void) {
	char config_path[512];
	snprintf(config_path, sizeof(config_path), "%s/config.toml", xdg_config_dir);

	/* Don't overwrite existing config */
	if (access(config_path, F_OK) == 0) {
		return true;
	}

	FILE *fp = fopen(config_path, "w");
	if (!fp) {
		fprintf(stderr, "Error: Could not create config file: %s\n", strerror(errno));
		return false;
	}

	const char *home = getenv("HOME");
	fprintf(fp, "# Glaciera Configuration File\n");
	fprintf(fp, "# Automatically generated on first run - edit as needed\n\n");

	fprintf(fp, "[paths]\n");
	fprintf(fp, "# Directories to index for music files (array of paths)\n");
	fprintf(fp, "# You can add multiple directories here\n");
	fprintf(fp, "index = [\n");
	fprintf(fp, "    \"%s/Music\",\n", home ? home : "/tmp");
	fprintf(fp, "    # Add more paths as needed:\n");
	fprintf(fp, "    # \"/mnt/music\",\n");
	fprintf(fp, "    # \"/media/external/audio\",\n");
	fprintf(fp, "]\n\n");
	fprintf(fp, "rippers = \"%s/Music/rippers\"\n\n", home ? home : "/tmp");

	fprintf(fp, "[players]\n");
	fprintf(fp, "mp3_player = \"mpg123\"\n");
	fprintf(fp, "mp3_flags = \"\"\n");
	fprintf(fp, "ogg_player = \"ogg123\"\n");
	fprintf(fp, "ogg_flags = \"\"\n");
	fprintf(fp, "flac_player = \"ogg123\"\n");
	fprintf(fp, "flac_flags = \"\"\n\n");

	fprintf(fp, "[appearance]\n");
	fprintf(fp, "# Theme name (default, or filename from themes/ directory without .toml)\n");
	fprintf(fp, "theme = \"default\"\n");

	fclose(fp);
	printf("\n");
	printf("══════════════════════════════════════════════════════════════════\n");
	printf(" Created default configuration at:\n");
	printf(" %s\n", config_path);
	printf("══════════════════════════════════════════════════════════════════\n");
	printf("\n");
	return true;
}

/* Load configuration from config.toml */
static bool load_config_file(void) {
	char config_path[512];
	snprintf(config_path, sizeof(config_path), "%s/config.toml", xdg_config_dir);

	FILE *fp = fopen(config_path, "r");
	if (!fp) {
		/* Config doesn't exist - that's okay, we'll create it */
		return config_create_default_file();
	}

	char errbuf[200];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf) {
		fprintf(stderr, "Error parsing config file: %s\n", errbuf);
		return false;
	}

	/* Parse [paths] section */
	toml_table_t *paths = toml_table_in(conf, "paths");
	if (paths) {
		/* Parse index paths array */
		toml_array_t *index_array = toml_array_in(paths, "index");
		if (index_array) {
			int count = toml_array_nelem(index_array);
			global_config.index_paths_count = 0;

			for (int i = 0; i < count && i < MAX_INDEX_PATHS; i++) {
				toml_datum_t path = toml_string_at(index_array, i);
				if (path.ok) {
					strncpy(global_config
						    .index_paths[global_config.index_paths_count],
						path.u.s, sizeof(global_config.index_paths[0]) - 1);
					global_config.index_paths_count++;
					free(path.u.s);
				}
			}

			if (global_config.index_paths_count > 0) {
				printf("Loaded %d index path(s) from config:\n",
				       global_config.index_paths_count);
				for (int i = 0; i < global_config.index_paths_count; i++) {
					printf("  [%d] %s\n", i + 1, global_config.index_paths[i]);
				}
			}
		} else {
			/* Fallback: try old single music_library path for backward compatibility */
			toml_datum_t music = toml_string_in(paths, "music_library");
			if (music.ok) {
				global_config.index_paths_count = 1;
				strncpy(global_config.index_paths[0], music.u.s,
					sizeof(global_config.index_paths[0]) - 1);
				free(music.u.s);
				printf("Note: Using legacy 'music_library' path. Consider updating "
				       "to 'index' array.\n");
			}
		}

		toml_datum_t rippers = toml_string_in(paths, "rippers");
		if (rippers.ok) {
			strncpy(global_config.rippers_path, rippers.u.s,
				sizeof(global_config.rippers_path) - 1);
			free(rippers.u.s);
		}
	}

	/* Parse [players] section */
	toml_table_t *players = toml_table_in(conf, "players");
	if (players) {
		toml_datum_t mp3 = toml_string_in(players, "mp3_player");
		if (mp3.ok) {
			strncpy(global_config.mp3_player_path, mp3.u.s,
				sizeof(global_config.mp3_player_path) - 1);
			free(mp3.u.s);
		}

		toml_datum_t mp3flags = toml_string_in(players, "mp3_flags");
		if (mp3flags.ok) {
			strncpy(global_config.mp3_player_flags, mp3flags.u.s,
				sizeof(global_config.mp3_player_flags) - 1);
			free(mp3flags.u.s);
		}

		toml_datum_t ogg = toml_string_in(players, "ogg_player");
		if (ogg.ok) {
			strncpy(global_config.ogg_player_path, ogg.u.s,
				sizeof(global_config.ogg_player_path) - 1);
			free(ogg.u.s);
		}

		toml_datum_t oggflags = toml_string_in(players, "ogg_flags");
		if (oggflags.ok) {
			strncpy(global_config.ogg_player_flags, oggflags.u.s,
				sizeof(global_config.ogg_player_flags) - 1);
			free(oggflags.u.s);
		}

		toml_datum_t flac = toml_string_in(players, "flac_player");
		if (flac.ok) {
			strncpy(global_config.flac_player_path, flac.u.s,
				sizeof(global_config.flac_player_path) - 1);
			free(flac.u.s);
		}

		toml_datum_t flacflags = toml_string_in(players, "flac_flags");
		if (flacflags.ok) {
			strncpy(global_config.flac_player_flags, flacflags.u.s,
				sizeof(global_config.flac_player_flags) - 1);
			free(flacflags.u.s);
		}
	}

	/* Parse [appearance] section */
	toml_table_t *appearance = toml_table_in(conf, "appearance");
	if (appearance) {
		toml_datum_t theme = toml_string_in(appearance, "theme");
		if (theme.ok) {
			if (strcmp(theme.u.s, "default") != 0) {
				config_load_theme(theme.u.s);
			}
			free(theme.u.s);
		}
	}

	toml_free(conf);
	return true;
}

/* Initialize configuration system */
bool config_init(void) {
	if (!init_xdg_dirs()) {
		return false;
	}

	/* Set defaults first */
	config_set_defaults(&global_config);

	/* Load config file (creates default if needed) */
	if (!load_config_file()) {
		fprintf(stderr, "Warning: Using default configuration\n");
	}

	printf("Config directory: %s\n", xdg_config_dir);
	printf("Data directory:   %s\n", xdg_data_dir);
	printf("Theme:            %s\n", global_config.theme.name);

	return true;
}

/* Get database path */
const char *config_get_db_path(void) {
	if (db_path_cache[0] == '\0') {
		snprintf(db_path_cache, sizeof(db_path_cache), "%s/glaciera.db", xdg_data_dir);
	}
	return db_path_cache;
}

/* Get primary music library path (first index path) */
const char *config_get_music_library_path(void) {
	if (global_config.index_paths_count > 0) {
		return global_config.index_paths[0];
	}
	return NULL;
}
