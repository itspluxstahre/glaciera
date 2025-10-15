#include "db.h"
#include "common.h"
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static sqlite3 *db = NULL;

static bool db_migrate_from_mmap(const char *db_path) {
	char old_db_path[1024];
	char db_dir[1024];
	int i;
	int f[5];
	struct stat ss;
	void *g_mm[5] = {0};
	int g_mmsize[5] = {0};
	struct tune0 *base0;
	int allcount = 0;
	int track_count = 0;

	/* Extract directory path from db_path */
	strcpy(db_dir, db_path);
	char *last_slash = strrchr(db_dir, '/');
	if (last_slash) {
		*last_slash = '\0';
	} else {
		strcpy(db_dir, ".");
	}

	/* Check if old database files exist */
	for (i = 0; i < 5; i++) {
		snprintf(old_db_path, sizeof(old_db_path), "%s/%d.db", db_dir, i);
		if (access(old_db_path, F_OK) != 0) {
			/* Old files don't exist, nothing to migrate */
			return true;
		}
	}

	fprintf(stderr, "Found old database files, migrating to SQLite...\n");

	/* Load old database files */
	for (i = 0; i < 5; i++) {
		snprintf(old_db_path, sizeof(old_db_path), "%s/%d.db", db_dir, i);
		f[i] = open(old_db_path, O_RDONLY);
		if (f[i] < 0) {
			fprintf(stderr, "Failed to open old database file %d.db\n", i);
			return false;
		}
		fstat(f[i], &ss);
		g_mmsize[i] = ss.st_size;
		if (i == 0) {
			allcount = g_mmsize[i] / sizeof(struct tune0);
		}
		g_mm[i] = mmap(0, g_mmsize[i], PROT_READ, MAP_SHARED, f[i], 0);
		close(f[i]);
	}

	if (allcount == 0) {
		/* No tracks to migrate */
		for (i = 0; i < 5; i++) {
			if (g_mm[i])
				munmap(g_mm[i], g_mmsize[i]);
		}
		return true;
	}

	/* Start transaction for batch insert */
	if (!db_begin_transaction()) {
		fprintf(stderr, "Failed to begin transaction for migration\n");
		return false;
	}

	/* Convert and insert tracks */
	char *path_data = (char *)g_mm[1];
	char *display_data = (char *)g_mm[2];
	char *search_data = (char *)g_mm[3];
	struct tuneinfo *ti_data = (struct tuneinfo *)g_mm[4];

	base0 = (struct tune0 *)g_mm[0];
	for (i = 0; i < allcount; i++) {
		char *path = path_data + base0->p1;
		char *display = display_data + base0->p2;
		char *search = search_data + base0->p3;
		struct tuneinfo ti = *ti_data;

		/* Insert track into SQLite */
		if (db_insert_track(path, display, search, &ti)) {
			track_count++;
		}

		/* Move to next track data */
		path_data += strlen(path) + 1;
		display_data += strlen(display) + 1;
		search_data += strlen(search) + 1;
		ti_data++;
		base0++;
	}

	/* Commit transaction */
	if (!db_commit_transaction()) {
		fprintf(stderr, "Failed to commit migration transaction\n");
		db_rollback_transaction();
		return false;
	}

	/* Clean up old files */
	for (i = 0; i < 5; i++) {
		snprintf(old_db_path, sizeof(old_db_path), "%s/%d.db", db_dir, i);
		unlink(old_db_path);

		if (g_mm[i]) {
			munmap(g_mm[i], g_mmsize[i]);
		}
	}

	fprintf(stderr, "Successfully migrated %d tracks to SQLite\n", track_count);
	return true;
}

/* Database initialization and management */
bool db_init(const char *db_path) {
	int rc;

	/* Create directory if it doesn't exist */
	char *dir = strdup(db_path);
	char *last_slash = strrchr(dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
		system(cmd);
		*last_slash = '/';
	}
	free(dir);

	rc = sqlite3_open(db_path, &db);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
		return false;
	}

	/* Enable WAL mode for better concurrency */
	sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);

	/* Migrate from old mmap format if needed */
	if (!db_migrate_from_mmap(db_path)) {
		sqlite3_close(db);
		db = NULL;
		return false;
	}

	/* Create schema if needed */
	if (!db_migrate()) {
		sqlite3_close(db);
		db = NULL;
		return false;
	}

	return true;
}

void db_close(void) {
	if (db) {
		sqlite3_close(db);
		db = NULL;
	}
}

bool db_migrate(void) {
	int rc;
	char *errmsg = NULL;

	/* Create tracks table */
	const char *create_tracks_sql =
	    "CREATE TABLE IF NOT EXISTS tracks ("
	    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
	    "    filepath TEXT NOT NULL UNIQUE,"
	    "    display_name TEXT NOT NULL,"
	    "    search_text TEXT NOT NULL,"
	    "    filesize INTEGER NOT NULL,"
	    "    filedate INTEGER NOT NULL,"
	    "    duration INTEGER NOT NULL,"
	    "    bitrate INTEGER NOT NULL,"
	    "    genre INTEGER NOT NULL,"
	    "    rating INTEGER NOT NULL,"
	    "    created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),"
	    "    updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))"
	    ");"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_filepath ON tracks(filepath);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_display_name ON tracks(display_name);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_search_text ON tracks(search_text);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_filesize ON tracks(filesize);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_filedate ON tracks(filedate);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);"
	    "CREATE INDEX IF NOT EXISTS idx_tracks_rating ON tracks(rating);";

	rc = sqlite3_exec(db, create_tracks_sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", errmsg);
		sqlite3_free(errmsg);
		return false;
	}

	return true;
}

/* Track operations */
bool db_insert_track(const char *filepath, const char *display_name, const char *search_text,
		     const struct tuneinfo *ti) {
	sqlite3_stmt *stmt;
	int rc;

	const char *sql = "INSERT INTO tracks (filepath, display_name, search_text, "
			  "filesize, filedate, duration, bitrate, genre, rating, updated_at) "
			  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now'))";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, display_name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, search_text, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, ti->filesize);
	sqlite3_bind_int64(stmt, 5, ti->filedate);
	sqlite3_bind_int(stmt, 6, ti->duration);
	sqlite3_bind_int(stmt, 7, ti->bitrate);
	sqlite3_bind_int(stmt, 8, ti->genre);
	sqlite3_bind_int(stmt, 9, ti->rating);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Failed to insert track: %s\n", sqlite3_errmsg(db));
		return false;
	}

	return true;
}

bool db_update_track(int id, const char *filepath, const char *display_name,
		     const char *search_text, const struct tuneinfo *ti) {
	sqlite3_stmt *stmt;
	int rc;

	const char *sql = "UPDATE tracks SET filepath=?, display_name=?, search_text=?, "
			  "filesize=?, filedate=?, duration=?, bitrate=?, genre=?, rating=?, "
			  "updated_at=strftime('%s', 'now') WHERE id=?";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, display_name, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, search_text, -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, ti->filesize);
	sqlite3_bind_int64(stmt, 5, ti->filedate);
	sqlite3_bind_int(stmt, 6, ti->duration);
	sqlite3_bind_int(stmt, 7, ti->bitrate);
	sqlite3_bind_int(stmt, 8, ti->genre);
	sqlite3_bind_int(stmt, 9, ti->rating);
	sqlite3_bind_int(stmt, 10, id);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Failed to update track: %s\n", sqlite3_errmsg(db));
		return false;
	}

	return true;
}

bool db_delete_track(int id) {
	sqlite3_stmt *stmt;
	int rc;

	const char *sql = "DELETE FROM tracks WHERE id=?";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_int(stmt, 1, id);

	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Failed to delete track: %s\n", sqlite3_errmsg(db));
		return false;
	}

	return true;
}

bool db_track_exists(const char *filepath) {
	sqlite3_stmt *stmt;
	int rc;
	bool exists = false;

	const char *sql = "SELECT COUNT(*) FROM tracks WHERE filepath=?";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return false;
	}

	sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		exists = sqlite3_column_int(stmt, 0) > 0;
	}

	sqlite3_finalize(stmt);
	return exists;
}

/* Track retrieval */
struct db_track *db_get_track_by_id(int id) {
	sqlite3_stmt *stmt;
	int rc;
	struct db_track *track = NULL;

	const char *sql = "SELECT id, filepath, display_name, search_text, "
			  "filesize, filedate, duration, bitrate, genre, rating, "
			  "created_at, updated_at FROM tracks WHERE id=?";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return NULL;
	}

	sqlite3_bind_int(stmt, 1, id);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		track = malloc(sizeof(struct db_track));
		if (!track) {
			sqlite3_finalize(stmt);
			return NULL;
		}

		track->id = sqlite3_column_int(stmt, 0);
		track->filepath = strdup((const char *)sqlite3_column_text(stmt, 1));
		track->display_name = strdup((const char *)sqlite3_column_text(stmt, 2));
		track->search_text = strdup((const char *)sqlite3_column_text(stmt, 3));

		track->ti.filesize = sqlite3_column_int(stmt, 4);
		track->ti.filedate = sqlite3_column_int64(stmt, 5);
		track->ti.duration = sqlite3_column_int(stmt, 6);
		track->ti.bitrate = sqlite3_column_int(stmt, 7);
		track->ti.genre = sqlite3_column_int(stmt, 8);
		track->ti.rating = sqlite3_column_int(stmt, 9);

		track->created_at = sqlite3_column_int64(stmt, 10);
		track->updated_at = sqlite3_column_int64(stmt, 11);
	}

	sqlite3_finalize(stmt);
	return track;
}

struct db_track *db_get_track_by_filepath(const char *filepath) {
	sqlite3_stmt *stmt;
	int rc;
	struct db_track *track = NULL;

	const char *sql = "SELECT id, filepath, display_name, search_text, "
			  "filesize, filedate, duration, bitrate, genre, rating, "
			  "created_at, updated_at FROM tracks WHERE filepath=?";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return NULL;
	}

	sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		track = malloc(sizeof(struct db_track));
		if (!track) {
			sqlite3_finalize(stmt);
			return NULL;
		}

		track->id = sqlite3_column_int(stmt, 0);
		track->filepath = strdup((const char *)sqlite3_column_text(stmt, 1));
		track->display_name = strdup((const char *)sqlite3_column_text(stmt, 2));
		track->search_text = strdup((const char *)sqlite3_column_text(stmt, 3));

		track->ti.filesize = sqlite3_column_int(stmt, 4);
		track->ti.filedate = sqlite3_column_int64(stmt, 5);
		track->ti.duration = sqlite3_column_int(stmt, 6);
		track->ti.bitrate = sqlite3_column_int(stmt, 7);
		track->ti.genre = sqlite3_column_int(stmt, 8);
		track->ti.rating = sqlite3_column_int(stmt, 9);

		track->created_at = sqlite3_column_int64(stmt, 10);
		track->updated_at = sqlite3_column_int64(stmt, 11);
	}

	sqlite3_finalize(stmt);
	return track;
}

struct db_track **db_search_tracks(const char *query, int *count) {
	sqlite3_stmt *stmt;
	int rc;
	struct db_track **tracks = NULL;
	int allocated = 0;
	*count = 0;

	if (!db) {
		fprintf(stderr, "ERROR: Database not initialized\n");
		return NULL;
	}

	/* Simple search: look for query in filepath, display_name, or search_text */
	const char *sql = "SELECT id, filepath, display_name, search_text, "
			  "filesize, filedate, duration, bitrate, genre, rating, "
			  "created_at, updated_at FROM tracks "
			  "WHERE filepath LIKE ? OR display_name LIKE ? OR search_text LIKE ? "
			  "ORDER BY display_name";

	char *pattern = malloc(strlen(query) + 3);
	sprintf(pattern, "%%%s%%", query);

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		free(pattern);
		return NULL;
	}

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_TRANSIENT);

	free(pattern);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (*count >= allocated) {
			allocated = allocated == 0 ? 16 : allocated * 2;
			tracks = realloc(tracks, allocated * sizeof(struct db_track *));
			if (!tracks) {
				sqlite3_finalize(stmt);
				return NULL;
			}
		}

		tracks[*count] = malloc(sizeof(struct db_track));
		if (!tracks[*count]) {
			sqlite3_finalize(stmt);
			return NULL;
		}

		tracks[*count]->id = sqlite3_column_int(stmt, 0);
		tracks[*count]->filepath = strdup((const char *)sqlite3_column_text(stmt, 1));
		tracks[*count]->display_name = strdup((const char *)sqlite3_column_text(stmt, 2));
		tracks[*count]->search_text = strdup((const char *)sqlite3_column_text(stmt, 3));

		tracks[*count]->ti.filesize = sqlite3_column_int(stmt, 4);
		tracks[*count]->ti.filedate = sqlite3_column_int64(stmt, 5);
		tracks[*count]->ti.duration = sqlite3_column_int(stmt, 6);
		tracks[*count]->ti.bitrate = sqlite3_column_int(stmt, 7);
		tracks[*count]->ti.genre = sqlite3_column_int(stmt, 8);
		tracks[*count]->ti.rating = sqlite3_column_int(stmt, 9);

		tracks[*count]->created_at = sqlite3_column_int64(stmt, 10);
		tracks[*count]->updated_at = sqlite3_column_int64(stmt, 11);

		(*count)++;
	}

	sqlite3_finalize(stmt);

	if (tracks && *count < allocated) {
		tracks = realloc(tracks, *count * sizeof(struct db_track *));
	}

	return tracks;
}

struct db_track **db_get_all_tracks(int *count) {
	return db_search_tracks("", count);
}

/* Batch operations for indexing */
bool db_begin_transaction(void) {
	return sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL) == SQLITE_OK;
}

bool db_commit_transaction(void) {
	return sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
}

bool db_rollback_transaction(void) {
	return sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK;
}

void db_insert_track_batch(const char *filepath, const char *display_name, const char *search_text,
			   const struct tuneinfo *ti) {
	db_insert_track(filepath, display_name, search_text, ti);
}

/* Statistics */
int db_get_track_count(void) {
	sqlite3_stmt *stmt;
	int rc, count = 0;

	const char *sql = "SELECT COUNT(*) FROM tracks";

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
		return 0;
	}

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);
	return count;
}

/* Memory management */
void db_free_track(struct db_track *track) {
	if (track) {
		free(track->filepath);
		free(track->display_name);
		free(track->search_text);
		free(track);
	}
}

void db_free_track_list(struct db_track **tracks, int count) {
	if (tracks) {
		for (int i = 0; i < count; i++) {
			db_free_track(tracks[i]);
		}
		free(tracks);
	}
}
