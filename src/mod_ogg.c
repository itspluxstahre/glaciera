// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>

// System headers
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <ogg/ogg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

// Local headers
#include "common.h"
#include "config.h"

/* -------------------------------------------------------------------------- */

static void ogg_metadata_set(char **dest, const char *value) {
	if (!value || !*value)
		return;
	if (*dest)
		return;
	*dest = strdup(value);
}

static void ogg_metadata_try_set_track(struct track_metadata *meta, const char *value) {
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

bool ogg_metadata(char *filename, struct track_metadata *meta) {
	if (!meta)
		return false;

	FILE *f = fopen(filename, "r");
	if (!f)
		return false;

	OggVorbis_File vf;
	memset(&vf, 0, sizeof(vf));
	if (ov_open(f, &vf, NULL, 0) < 0) {
		fclose(f);
		return false;
	}

	bool found = false;
	vorbis_comment *comment = ov_comment(&vf, -1);
	if (comment) {
		for (int i = 0; i < comment->comments; i++) {
			char *entry = comment->user_comments[i];
			if (!entry)
				continue;
			char *sep = strchr(entry, '=');
			if (!sep)
				continue;
			size_t key_len = (size_t)(sep - entry);
			const char *value = sep + 1;
			if (!*value)
				continue;

			if (key_len == 5 && strncasecmp(entry, "TITLE", 5) == 0) {
				ogg_metadata_set(&meta->title, value);
				if (meta->title)
					found = true;
			} else if (key_len == 6 && strncasecmp(entry, "ARTIST", 6) == 0) {
				ogg_metadata_set(&meta->artist, value);
				if (meta->artist)
					found = true;
			} else if (key_len == 5 && strncasecmp(entry, "ALBUM", 5) == 0) {
				ogg_metadata_set(&meta->album, value);
				if (meta->album)
					found = true;
			} else if ((key_len == 11 && strncasecmp(entry, "TRACKNUMBER", 11) == 0)
			    || (key_len == 5 && strncasecmp(entry, "TRACK", 5) == 0)) {
				ogg_metadata_try_set_track(meta, value);
				if (meta->track || meta->track_number >= 0)
					found = true;
			}
		}
	}

	ov_clear(&vf);
	return found;
}

/* -------------------------------------------------------------------------- */

bool ogg_info(char *filename, struct tuneinfo *ti) {
	FILE *f;
	OggVorbis_File vf;
	int error;
	struct stat ss;

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "\nogg_read_info: fail in open '%s'\n", filename);
		return false;
	}

	memset(&vf, 0, sizeof(OggVorbis_File));
	error = ov_open(f, &vf, NULL, 0);
	if (error < 0) {
		fclose(f);
		fprintf(stderr, "\nogg_read_info: Unable to understand '%s', errorcode=%d\n",
		    filename, error);
		return false;
	}

	error = stat(filename, &ss);
	if (!error) {
		ti->filesize = ss.st_size;
		ti->filedate = ss.st_mtime;
	}

	ti->duration = ov_time_total(&vf, -1);
	ti->bitrate = ov_bitrate(&vf, -1) / 1000;

	/*
	 * Yes, just call ov_clear()... NOT fclose()
	 * fclose() is done _inside_ of ov_clear()
	 */
	ov_clear(&vf);
	return true;
}

bool ogg_isit(char *s, int len) {
	if (len < 5)
		return false;

	const char *ext = s + len - 4;
	return (strcasecmp(ext, ".ogg") == 0);
}

void ogg_play(char *filename) {
	const char *player = config_get_ogg_player_path();
	execlp(player, player, filename, NULL);
}
