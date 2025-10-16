// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2025 Glaciera Contributors

// System headers
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* Print a single color row in the theme preview table */
static void print_color_row(const char *label, int r, int g, int b) {
	char hex[8];
	rgb_to_hex(r, g, b, hex);
	
	int label_len = strlen(label);
	int pad = 12 - label_len;
	
	printf("│ %s", label);
	for (int i = 0; i < pad; i++)
		printf(" ");
	
	printf(" │ ");
	print_color_block(r, g, b);
	printf(" │ %s │\n", hex);
}

/* Print a single theme preview */
static void print_theme_preview(const theme_t *theme) {
	printf("╭───────────────────────────────────────────────╮\n");
	printf("│ Theme: %-38s │\n", theme->name);
	printf("├──────────────┬──────────────────────┬─────────┤\n");

	/* Print each color property with column separators
	 * Format: │ name(12) │ color_block(20) │ hex(7) │ */
	print_color_row("main_bg", theme->main_bg.r, theme->main_bg.g, theme->main_bg.b);
	print_color_row("main_fg", theme->main_fg.r, theme->main_fg.g, theme->main_fg.b);
	print_color_row("accent_bg", theme->accent_bg.r, theme->accent_bg.g, theme->accent_bg.b);
	print_color_row("accent_fg", theme->accent_fg.r, theme->accent_fg.g, theme->accent_fg.b);
	print_color_row("playing", theme->playing.r, theme->playing.g, theme->playing.b);
	print_color_row("playlist", theme->playlist.r, theme->playlist.g, theme->playlist.b);
	print_color_row("highlight_bg", theme->highlight_bg.r, theme->highlight_bg.g,
	    theme->highlight_bg.b);
	print_color_row("highlight_fg", theme->highlight_fg.r, theme->highlight_fg.g,
	    theme->highlight_fg.b);

	printf("╰──────────────┴──────────────────────┴─────────╯\n\n");
}

/* Helper function to parse RGB color from TOML sub-table */
static void parse_color(toml_table_t *colors, const char *name, int *r_out, int *g_out,
    int *b_out) {
	toml_table_t *color_table = toml_table_in(colors, name);
	if (!color_table)
		return;

	toml_datum_t r = toml_int_in(color_table, "r");
	toml_datum_t g = toml_int_in(color_table, "g");
	toml_datum_t b = toml_int_in(color_table, "b");

	if (r.ok && g.ok && b.ok) {
		*r_out = r.u.i;
		*g_out = g.u.i;
		*b_out = b.u.i;
	}
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

	/* Parse top-level name */
	toml_datum_t name = toml_string_in(conf, "name");
	if (name.ok) {
		strncpy(theme->name, name.u.s, sizeof(theme->name) - 1);
		free(name.u.s);
	}

	/* Parse [colors] section */
	toml_table_t *colors = toml_table_in(conf, "colors");
	if (!colors) {
		toml_free(conf);
		return false;
	}

	parse_color(colors, "main_bg", &theme->main_bg.r, &theme->main_bg.g, &theme->main_bg.b);
	parse_color(colors, "main_fg", &theme->main_fg.r, &theme->main_fg.g, &theme->main_fg.b);
	parse_color(colors, "accent_bg", &theme->accent_bg.r, &theme->accent_bg.g,
	    &theme->accent_bg.b);
	parse_color(colors, "accent_fg", &theme->accent_fg.r, &theme->accent_fg.g,
	    &theme->accent_fg.b);
	parse_color(colors, "playing", &theme->playing.r, &theme->playing.g, &theme->playing.b);
	parse_color(colors, "playlist", &theme->playlist.r, &theme->playlist.g, &theme->playlist.b);
	parse_color(colors, "highlight_bg", &theme->highlight_bg.r, &theme->highlight_bg.g,
	    &theme->highlight_bg.b);
	parse_color(colors, "highlight_fg", &theme->highlight_fg.r, &theme->highlight_fg.g,
	    &theme->highlight_fg.b);

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
	printf("═════════════════════════════════════════════════════════\n");
	printf("               GLACIERA THEME PREVIEW\n");
	printf("═════════════════════════════════════════════════════════\n");
	printf("\n");

	struct dirent *entry;
	int theme_count = 0;

	while ((entry = readdir(dir)) != NULL) {
		/* Skip hidden files and . / .. */
		if (entry->d_name[0] == '.')
			continue;

		/* Only process .toml files */
		size_t len = strlen(entry->d_name);
		if (len < 6 || strcmp(entry->d_name + len - 5, ".toml") != 0)
			continue;

		/* Build full path and verify it's a regular file */
		char theme_path[1024];
		snprintf(theme_path, sizeof(theme_path), "%s/%s", themes_dir, entry->d_name);

		struct stat st;
		if (stat(theme_path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;

		/* Load and display theme */
		theme_t theme = { 0 };
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
