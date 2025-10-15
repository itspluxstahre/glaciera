/*
 * config.h - Modern XDG-compliant configuration system for Glaciera
 *
 * Copyright (c) 2025 Glaciera Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/* Color theme structure */
typedef struct {
    char name[64];
    /* RGB values for each semantic color (0-255) */
    struct {
        int r, g, b;
    } main_bg, main_fg, accent_bg, accent_fg;
    struct {
        int r, g, b;
    } playing, playlist, highlight_bg, highlight_fg;
} theme_t;

#define MAX_INDEX_PATHS 16

/* Main configuration structure */
typedef struct {
    /* Multiple paths to index */
    char index_paths[MAX_INDEX_PATHS][512];
    int index_paths_count;
    
    char rippers_path[512];
    char mp3_player_path[128];
    char mp3_player_flags[256];
    char ogg_player_path[128];
    char ogg_player_flags[256];
    char flac_player_path[128];
    char flac_player_flags[256];
    
    /* Active theme */
    theme_t theme;
    char theme_name[64];  /* Name of loaded theme file, or "default" */
} config_t;

/* XDG directory paths */
extern char xdg_config_dir[512];   /* ~/.config/glaciera */
extern char xdg_data_dir[512];     /* ~/.local/share/glaciera */
extern char xdg_cache_dir[512];    /* ~/.cache/glaciera */

/* Global config instance */
extern config_t global_config;

/* Initialize XDG directories and load configuration */
bool config_init(void);

/* Load theme from file (looks in themes/ subdirectory) */
bool config_load_theme(const char *theme_name);

/* Get default configuration values */
void config_set_defaults(config_t *config);

/* Create default config file if it doesn't exist */
bool config_create_default_file(void);

/* Get the database path (in XDG_DATA_HOME) */
const char *config_get_db_path(void);

/* Get primary music library path (first index path or default) */
const char *config_get_music_library_path(void);

#endif /* CONFIG_H */
