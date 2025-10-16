#pragma once

#include "common.h"
#include <sqlite3.h>
#include <stdbool.h>

struct db_track {
	int id;
	char *filepath;
	char *display_name;
	char *search_text;
	struct tuneinfo ti;
	time_t created_at;
	time_t updated_at;
};

/* Database initialization and management */
bool db_init(const char *db_path);
void db_close(void);
bool db_migrate(void);

/* Track operations */
bool db_insert_track(const char *filepath, const char *display_name, const char *search_text,
    const struct tuneinfo *ti);
bool db_update_track(int id, const char *filepath, const char *display_name,
    const char *search_text, const struct tuneinfo *ti);
bool db_delete_track(int id);
bool db_track_exists(const char *filepath);

/* Track retrieval */
struct db_track *db_get_track_by_id(int id);
struct db_track *db_get_track_by_filepath(const char *filepath);
struct db_track **db_search_tracks(const char *query, int *count);
struct db_track **db_get_all_tracks(int *count);

/* Batch operations for indexing */
bool db_begin_transaction(void);
bool db_commit_transaction(void);
bool db_rollback_transaction(void);
void db_insert_track_batch(const char *filepath, const char *display_name, const char *search_text,
    const struct tuneinfo *ti);

/* Statistics */
int db_get_track_count(void);

/* Memory management */
void db_free_track(struct db_track *track);
void db_free_track_list(struct db_track **tracks, int count);
