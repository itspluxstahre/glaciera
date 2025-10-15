// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2025 Glaciera Contributors

// System headers
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Local headers
#include "config.h"
#include "toml.h"

/* Print colored block using ANSI true color escape codes */
static void print_color_block(int r, int g, int b) {
	printf("\033[48;2;%d;%d;%dm                    \033[0m", r, g, b);
}

/* Convert RGB to hex string */
static void rgb_to_hex(int r, int g, int b, char *hex) {
	sprintf(hex, "#%02x%02x%02x", r, g, b);
}

/* Print a single theme preview */
static void print_theme_preview(const theme_t *theme) {
	char hex[8];

	printf("╭─────────────────────────────────────────────────────────────╮\n");
	printf("│ Theme: %-49s │\n", theme->name);
	printf("├─────────────────────────────────────────────────────────────┤\n");

	/* Print each color property */
	printf("│ main_bg      ");
	print_color_block(theme->main_bg.r, theme->main_bg.g, theme->main_bg.b);
	rgb_to_hex(theme->main_bg.r, theme->main_bg.g, theme->main_bg.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ main_fg      ");
	print_color_block(theme->main_fg.r, theme->main_fg.g, theme->main_fg.b);
	rgb_to_hex(theme->main_fg.r, theme->main_fg.g, theme->main_fg.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ accent_bg    ");
	print_color_block(theme->accent_bg.r, theme->accent_bg.g, theme->accent_bg.b);
	rgb_to_hex(theme->accent_bg.r, theme->accent_bg.g, theme->accent_bg.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ accent_fg    ");
	print_color_block(theme->accent_fg.r, theme->accent_fg.g, theme->accent_fg.b);
	rgb_to_hex(theme->accent_fg.r, theme->accent_fg.g, theme->accent_fg.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ playing      ");
	print_color_block(theme->playing.r, theme->playing.g, theme->playing.b);
	rgb_to_hex(theme->playing.r, theme->playing.g, theme->playing.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ playlist     ");
	print_color_block(theme->playlist.r, theme->playlist.g, theme->playlist.b);
	rgb_to_hex(theme->playlist.r, theme->playlist.g, theme->playlist.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ highlight_bg ");
	print_color_block(theme->highlight_bg.r, theme->highlight_bg.g, theme->highlight_bg.b);
	rgb_to_hex(theme->highlight_bg.r, theme->highlight_bg.g, theme->highlight_bg.b, hex);
	printf("  %-7s │\n", hex);

	printf("│ highlight_fg ");
	print_color_block(theme->highlight_fg.r, theme->highlight_fg.g, theme->highlight_fg.b);
	rgb_to_hex(theme->highlight_fg.r, theme->highlight_fg.g, theme->highlight_fg.b, hex);
	printf("  %-7s │\n", hex);

	printf("╰─────────────────────────────────────────────────────────────╯\n\n");
}

/* Convert hex color to RGB */
static void hex_to_rgb_struct(const char *hex, int *r, int *g, int *b) {
	unsigned int color;
	sscanf(hex + 1, "%x", &color); /* Skip # */
	*r = (color >> 16) & 0xFF;
	*g = (color >> 8) & 0xFF;
	*b = color & 0xFF;
}

/* Load a single theme file */
static bool load_theme_file(const char *theme_path, theme_t *theme) {
	FILE *fp = fopen(theme_path, "r");
	if (!fp)
		return false;

	char errbuf[200];
	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf)
		return false;

	/* Parse theme section */
	toml_table_t *theme_table = toml_table_in(conf, "theme");
	if (!theme_table) {
		toml_free(conf);
		return false;
	}

	/* Parse theme properties */
	toml_datum_t name = toml_string_in(theme_table, "name");
	toml_datum_t main_bg = toml_string_in(theme_table, "main_bg");
	toml_datum_t main_fg = toml_string_in(theme_table, "main_fg");
	toml_datum_t accent_bg = toml_string_in(theme_table, "accent_bg");
	toml_datum_t accent_fg = toml_string_in(theme_table, "accent_fg");
	toml_datum_t playing = toml_string_in(theme_table, "playing");
	toml_datum_t playlist = toml_string_in(theme_table, "playlist");
	toml_datum_t highlight_bg = toml_string_in(theme_table, "highlight_bg");
	toml_datum_t highlight_fg = toml_string_in(theme_table, "highlight_fg");

	if (name.ok)
		strncpy(theme->name, name.u.s, sizeof(theme->name) - 1);
	if (main_bg.ok)
		hex_to_rgb_struct(main_bg.u.s, &theme->main_bg.r, &theme->main_bg.g,
		                  &theme->main_bg.b);
	if (main_fg.ok)
		hex_to_rgb_struct(main_fg.u.s, &theme->main_fg.r, &theme->main_fg.g,
		                  &theme->main_fg.b);
	if (accent_bg.ok)
		hex_to_rgb_struct(accent_bg.u.s, &theme->accent_bg.r, &theme->accent_bg.g,
		                  &theme->accent_bg.b);
	if (accent_fg.ok)
		hex_to_rgb_struct(accent_fg.u.s, &theme->accent_fg.r, &theme->accent_fg.g,
		                  &theme->accent_fg.b);
	if (playing.ok)
		hex_to_rgb_struct(playing.u.s, &theme->playing.r, &theme->playing.g,
		                  &theme->playing.b);
	if (playlist.ok)
		hex_to_rgb_struct(playlist.u.s, &theme->playlist.r, &theme->playlist.g,
		                  &theme->playlist.b);
	if (highlight_bg.ok)
		hex_to_rgb_struct(highlight_bg.u.s, &theme->highlight_bg.r, &theme->highlight_bg.g,
		                  &theme->highlight_bg.b);
	if (highlight_fg.ok)
		hex_to_rgb_struct(highlight_fg.u.s, &theme->highlight_fg.r, &theme->highlight_fg.g,
		                  &theme->highlight_fg.b);

	/* Free strings */
	if (name.ok)
		free(name.u.s);
	if (main_bg.ok)
		free(main_bg.u.s);
	if (main_fg.ok)
		free(main_fg.u.s);
	if (accent_bg.ok)
		free(accent_bg.u.s);
	if (accent_fg.ok)
		free(accent_fg.u.s);
	if (playing.ok)
		free(playing.u.s);
	if (playlist.ok)
		free(playlist.u.s);
	if (highlight_bg.ok)
		free(highlight_bg.u.s);
	if (highlight_fg.ok)
		free(highlight_fg.u.s);

	toml_free(conf);
	return true;
}

/* Display all themes in the themes directory */
void display_theme_previews(const char *themes_dir) {
	DIR *dir = opendir(themes_dir);
	if (!dir) {
		fprintf(stderr, "Error: Could not open themes directory: %s\n", themes_dir);
		return;
	}

	printf("\n");
	printf("═══════════════════════════════════════════════════════════════\n");
	printf("                    GLACIERA THEME PREVIEW\n");
	printf("═══════════════════════════════════════════════════════════════\n");
	printf("\n");

	struct dirent *entry;
	int theme_count = 0;

	while ((entry = readdir(dir)) != NULL) {
		/* Only process .toml files */
		if (entry->d_type != DT_REG)
			continue;

		size_t len = strlen(entry->d_name);
		if (len < 6 || strcmp(entry->d_name + len - 5, ".toml") != 0)
			continue;

		/* Load and display theme */
		char theme_path[1024];
		snprintf(theme_path, sizeof(theme_path), "%s/%s", themes_dir, entry->d_name);

		theme_t theme = {0};
		if (load_theme_file(theme_path, &theme)) {
			print_theme_preview(&theme);
			theme_count++;
		}
	}

	closedir(dir);

	if (theme_count == 0) {
		printf("No themes found in: %s\n\n", themes_dir);
	} else {
		printf("Found %d theme%s in: %s\n\n", theme_count, theme_count == 1 ? "" : "s",
		       themes_dir);
	}
}
