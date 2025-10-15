// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <FLAC/all.h>
#include <limits.h>
#include <strings.h>

#include "common.h"

/* -------------------------------------------------------------------------- */

bool flac_info(char *filename, struct tuneinfo *ti) {
	int error;
	struct stat ss;
	FLAC__StreamMetadata md;

	error = stat(filename, &ss);
	if (error) {
		fprintf(stderr, "\nflac_read_info: fail in open '%s'\n", filename);
		return false;
	}

	FLAC__metadata_get_streaminfo(filename, &md);

	ti->filesize = ss.st_size;
	ti->filedate = ss.st_mtime;
	if (md.data.stream_info.sample_rate)
		ti->duration = md.data.stream_info.total_samples / md.data.stream_info.sample_rate;
	else
		ti->duration = 0;
	ti->bitrate = 128; /* TODO: Get values from file */
	ti->genre = 0xff;  /* TODO: Get values from file */

	return true;
}

static void flac_metadata_set(char **dest, const char *value) {
	if (!value || !*value)
		return;
	if (*dest)
		return;
	*dest = strdup(value);
}

static void flac_metadata_try_set_track(struct track_metadata *meta, const char *value) {
	if (!value || !*value)
		return;
	if (!meta->track)
		meta->track = strdup(value);
	if (meta->track_number < 0) {
		char *endptr = NULL;
		long parsed = strtol(value, &endptr, 10);
		if (parsed > 0 && parsed < INT_MAX)
			meta->track_number = (int)parsed;
	}
}

bool flac_metadata(char *filename, struct track_metadata *meta) {
	if (!meta)
		return false;

	FLAC__StreamMetadata *tags = NULL;
	if (!FLAC__metadata_get_tags(filename, &tags))
		return false;

	bool found = false;
	if (tags && tags->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
		FLAC__StreamMetadata_VorbisComment *vc = &tags->data.vorbis_comment;
		for (unsigned int i = 0; i < vc->num_comments; i++) {
			const FLAC__StreamMetadata_VorbisComment_Entry *entry = &vc->comments[i];
			if (!entry->entry || entry->length == 0)
				continue;
			char *comment = malloc(entry->length + 1);
			if (!comment)
				continue;
			memcpy(comment, entry->entry, entry->length);
			comment[entry->length] = '\0';

			char *sep = strchr(comment, '=');
			if (sep) {
				size_t key_len = (size_t)(sep - comment);
				const char *value = sep + 1;
				if (*value) {
					if (key_len == 5 && strncasecmp(comment, "TITLE", 5) == 0) {
						flac_metadata_set(&meta->title, value);
						if (meta->title)
							found = true;
					} else if (key_len == 6 &&
						   strncasecmp(comment, "ARTIST", 6) == 0) {
						flac_metadata_set(&meta->artist, value);
						if (meta->artist)
							found = true;
					} else if (key_len == 5 &&
						   strncasecmp(comment, "ALBUM", 5) == 0) {
						flac_metadata_set(&meta->album, value);
						if (meta->album)
							found = true;
					} else if ((key_len == 11 &&
						    strncasecmp(comment, "TRACKNUMBER", 11) == 0) ||
						   (key_len == 5 &&
						    strncasecmp(comment, "TRACK", 5) == 0)) {
						flac_metadata_try_set_track(meta, value);
						if (meta->track || meta->track_number >= 0)
							found = true;
					}
				}
			}
			free(comment);
		}
	}

	if (tags)
		FLAC__metadata_object_delete(tags);
	return found;
}

/* -------------------------------------------------------------------------- */

bool flac_isit(char *s, int len) {
	if ((len > 5) && (s[len - 5] == '.') && (s[len - 4] == 'F' || s[len - 4] == 'f') &&
	    (s[len - 3] == 'L' || s[len - 3] == 'l') && (s[len - 2] == 'A' || s[len - 2] == 'a') &&
	    (s[len - 1] == 'C' || s[len - 1] == 'c')) {
		return true;
	}

	return false;
}

void flac_play(char *filename) {
	/*
	 * Uhm... discovered that ogg123 DOES infact play .flac-files,
	 * even better than flac123.
	 *
	 * flac123 has some strange bugs...
	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=339450
	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=339454
	 *
	 * But now we have a dedicated FLAC player config option!
	 */
	char *player_args[3];
	player_args[0] = opt_flacplayerpath;
	player_args[1] = filename;
	player_args[2] = NULL;

	if (strlen(opt_flacplayerflags) > 0) {
		/* If flags are configured, we need to split them */
		char flags_copy[256];
		strncpy(flags_copy, opt_flacplayerflags, sizeof(flags_copy) - 1);
		flags_copy[sizeof(flags_copy) - 1] = '\0';

		/* Simple approach: if flags exist, add them before filename */
		char *flag_args[4];
		flag_args[0] = opt_flacplayerpath;
		flag_args[1] = flags_copy;
		flag_args[2] = filename;
		flag_args[3] = NULL;

		execvp(opt_flacplayerpath, flag_args);
	} else {
		execvp(opt_flacplayerpath, player_args);
	}
}
