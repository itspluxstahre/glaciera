// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
// Scanning code based on C# code from:
// http://www.developeru.info/PermaLink,guid,6ac0aff9-0223-4a05-94ac-995feaac683a.aspx

// System headers
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Local headers
#include "common.h"
#include "config.h"

/* --------------------------------------------------------------------------- */

/* 3         2         1                      */
/*10987654321098765432109876543210            */
/*          FFFFFF                 Framesync  */
/*            VV                              */
/*              LL                            */
/*                   BBBB                     */
/*                                            */
/*                                            */
/*                                            */
/*                                            */
/*                              EE Emphasis   */

static inline int mp3_getFrameSync(unsigned long h) {
	return (int)((h >> 21) & 0x7FF);
}

static inline int mp3_getVersionIndex(unsigned long h) {
	return (int)((h >> 19) & 0x03);
}

static inline int mp3_getLayerIndex(unsigned long h) {
	return (int)((h >> 17) & 0x03);
}

static inline int mp3_getBitrateIndex(unsigned long h) {
	return (int)((h >> 12) & 0x0F);
}

static inline int mp3_getFrequencyIndex(unsigned long h) {
	return (int)((h >> 10) & 0x03);
}

static inline int mp3_getEmphasisIndex(unsigned long h) {
	return (int)(h & 0x03);
}

static inline int mp3_getModeIndex(unsigned long h) {
	return (int)((h >> 6) & 0x03);
}

static inline int mp3_is_valid_header(unsigned long h) {
	return (((mp3_getFrameSync(h)) == 0x7FF) && ((mp3_getVersionIndex(h)) != 1) &&
		((mp3_getLayerIndex(h)) != 0) && ((mp3_getBitrateIndex(h)) != 0) &&
		((mp3_getBitrateIndex(h)) != 15) && ((mp3_getFrequencyIndex(h)) != 3) &&
		((mp3_getEmphasisIndex(h)) != 2));
}

static inline int mp3_get_frequency(unsigned long h) {
	static int tableFreq[4][3] = {
	    {32000, 16000, 8000},  /* MPEG 2.5 */
	    {0, 0, 0},             /* reserved */
	    {22050, 24000, 16000}, /* MPEG 2   */
	    {44100, 48000, 32000}  /* MPEG 1   */
	};

	return tableFreq[mp3_getVersionIndex(h)][mp3_getFrequencyIndex(h)];
}

static int mp3_calc_bit_rate(unsigned long h, size_t filesize, int variable_frames) {
	static int tableBitRate[2][3][16] = {
	    {
		/* MPEG 2 & 2.5 */
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}, /* Layer III */
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}, /* Layer II */
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0} /* Layer I */
	    },
	    {
		/* MPEG 1 */
		{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
		 0}, /* Layer III */
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,
		 0}, /* Layer II */
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}
		/* Layer I */
	    }};

	int result = 0;

	/*
	 * If the file has a variable bitrate, then we return
	 * an integer average bitrate, otherwise, we use a lookup
	 * table to return the bitrate
	 */
	if (variable_frames) {
		double medFrameSize = (double)filesize / (double)variable_frames;
		result = (int)((medFrameSize * (double)mp3_get_frequency(h)) /
			       (1000.0 * ((mp3_getLayerIndex(h) == 3) ? 12.0 : 144.0)));
	} else {
		result = tableBitRate[mp3_getVersionIndex(h) & 1][mp3_getLayerIndex(h) - 1]
				     [mp3_getBitrateIndex(h)];
	}

	return result;
}

static inline long mp3_calc_length_in_seconds(size_t filesize, int bitrate) {
	int intKiloBitFileSize = (int)((8 * filesize) / 1000);
	return bitrate ? ((long)(intKiloBitFileSize / bitrate)) : 0;
}

struct mp3_id3v1 {
	char id[3]; /* should be TAG */
	char title[30];
	char artist[30];
	char album[30];
	char year[4];
	char comment[30];
	unsigned char genre;
};

static char *mp3_dup_trim_ascii(const unsigned char *src, size_t len) {
	size_t start = 0;
	size_t end = len;

	while (start < len && (src[start] == '\0' || isspace((unsigned char)src[start])))
		start++;
	while (end > start && (src[end - 1] == '\0' || isspace((unsigned char)src[end - 1])))
		end--;
	if (end <= start)
		return NULL;

	char *out = malloc(end - start + 1);
	if (!out)
		return NULL;
	memcpy(out, src + start, end - start);
	out[end - start] = '\0';
	return out;
}

static void metadata_set_if_empty(char **dest, char *value) {
	if (!value)
		return;
	if (*value == '\0') {
		free(value);
		return;
	}
	if (*dest) {
		free(value);
		return;
	}
	*dest = value;
}

static void metadata_try_set_track_number(struct track_metadata *meta, const char *value) {
	if (!value || meta->track_number >= 0)
		return;
	char *endptr = NULL;
	long parsed = strtol(value, &endptr, 10);
	if (parsed > 0 && parsed < INT_MAX)
		meta->track_number = (int)parsed;
}

static uint32_t mp3_read_be32(const unsigned char *data) {
	return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
	       ((uint32_t)data[3]);
}

static uint32_t mp3_read_synchsafe32(const unsigned char *data) {
	return ((uint32_t)(data[0] & 0x7f) << 21) | ((uint32_t)(data[1] & 0x7f) << 14) |
	       ((uint32_t)(data[2] & 0x7f) << 7) | ((uint32_t)(data[3] & 0x7f));
}

static char *mp3_decode_utf16(const unsigned char *data, size_t len, bool big_endian) {
	if (len < 2)
		return NULL;

	size_t out_cap = len * 2 + 1;
	char *out = malloc(out_cap);
	if (!out)
		return NULL;

	size_t out_len = 0;
	for (size_t i = 0; i + 1 < len; i += 2) {
		uint16_t code = big_endian ? (uint16_t)((data[i] << 8) | data[i + 1])
					   : (uint16_t)((data[i + 1] << 8) | data[i]);
		if (code == 0)
			break;

		uint32_t value = code;

		if (code >= 0xD800 && code <= 0xDBFF) {
			if (i + 3 >= len)
				break;
			uint16_t low = big_endian ? (uint16_t)((data[i + 2] << 8) | data[i + 3])
						  : (uint16_t)((data[i + 3] << 8) | data[i + 2]);
			if (low >= 0xDC00 && low <= 0xDFFF) {
				value = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
				i += 2;
			} else {
				continue;
			}
		}

		if (value <= 0x7F) {
			out[out_len++] = (char)value;
		} else if (value <= 0x7FF) {
			out[out_len++] = (char)(0xC0 | (value >> 6));
			out[out_len++] = (char)(0x80 | (value & 0x3F));
		} else if (value <= 0xFFFF) {
			out[out_len++] = (char)(0xE0 | (value >> 12));
			out[out_len++] = (char)(0x80 | ((value >> 6) & 0x3F));
			out[out_len++] = (char)(0x80 | (value & 0x3F));
		} else {
			out[out_len++] = (char)(0xF0 | (value >> 18));
			out[out_len++] = (char)(0x80 | ((value >> 12) & 0x3F));
			out[out_len++] = (char)(0x80 | ((value >> 6) & 0x3F));
			out[out_len++] = (char)(0x80 | (value & 0x3F));
		}

		if (out_len + 4 >= out_cap) {
			out_cap *= 2;
			char *tmp = realloc(out, out_cap);
			if (!tmp) {
				free(out);
				return NULL;
			}
			out = tmp;
		}
	}

	out[out_len] = '\0';
	return out;
}

/*
 * Decode ID3 text based on encoding type
 * Returns allocated string (caller must free) or NULL on error
 */
static char *mp3_decode_id3_text(uint8_t encoding, const unsigned char *data, size_t len) {
	if (!data || len == 0)
		return NULL;

	switch (encoding) {
	case 0: /* ISO-8859-1 */
		return mp3_dup_trim_ascii(data, len);
	case 3: /* UTF-8 */
		return mp3_dup_trim_ascii(data, strnlen((const char *)data, len));
	case 1: /* UTF-16 with BOM */
	{
		bool big_endian = true;
		size_t offset = 0;
		if (len >= 2) {
			if (data[0] == 0xFF && data[1] == 0xFE) {
				big_endian = false;
				offset = 2;
			} else if (data[0] == 0xFE && data[1] == 0xFF) {
				big_endian = true;
				offset = 2;
			}
		}
		if (offset >= len)
			return NULL;
		return mp3_decode_utf16(data + offset, len - offset, big_endian);
	}
	case 2: /* UTF-16BE without BOM */
		return mp3_decode_utf16(data, len, true);
	default:
		return NULL;
	}
}

/*
 * Parse a single ID3v2 text frame and store in metadata
 * Returns true if metadata was successfully extracted
 */
static bool parse_id3v2_text_frame(const char *frame_id, const unsigned char *frame_data,
				   uint32_t frame_size, struct track_metadata *meta) {
	if (!frame_id || !frame_data || !meta || frame_size < 2)
		return false;

	/* Only handle text frames (T***) */
	if (frame_id[0] != 'T')
		return false;

	uint8_t encoding = frame_data[0];
	char *value = mp3_decode_id3_text(encoding, frame_data + 1, frame_size - 1);
	if (!value)
		return false;

	bool handled = false;
	if (strcmp(frame_id, "TIT2") == 0) {
		/* Title */
		metadata_set_if_empty(&meta->title, value);
		handled = (meta->title == value);
	} else if (strcmp(frame_id, "TPE1") == 0) {
		/* Artist */
		metadata_set_if_empty(&meta->artist, value);
		handled = (meta->artist == value);
	} else if (strcmp(frame_id, "TALB") == 0) {
		/* Album */
		metadata_set_if_empty(&meta->album, value);
		handled = (meta->album == value);
	} else if (strcmp(frame_id, "TRCK") == 0) {
		/* Track number */
		metadata_try_set_track_number(meta, value);
		metadata_set_if_empty(&meta->track, value);
		handled = (meta->track == value || meta->track_number >= 0);
	} else {
		/* Unknown frame, discard */
		free(value);
	}

	return handled;
}

/*
 * Decode and parse a single ID3v2 frame
 * Returns frame size (including header) or 0 on error
 */
static uint32_t decode_id3v2_frame(const unsigned char *frame, size_t available_size,
				   uint8_t version, struct track_metadata *meta,
				   bool *found_metadata) {
	/* Need at least 10 bytes for frame header */
	if (!frame || !meta || available_size < 10)
		return 0;

	/* Check for padding (null bytes indicate end of frames) */
	if (frame[0] == 0)
		return 0;

	/* Extract frame ID */
	char frame_id[5];
	memcpy(frame_id, frame, 4);
	frame_id[4] = '\0';

	/* Validate frame ID (should be alphanumeric) */
	for (int i = 0; i < 4; i++) {
		if (!isalnum((unsigned char)frame_id[i]))
			return 0;
	}

	/* Read frame size */
	uint32_t frame_size =
	    (version == 4) ? mp3_read_synchsafe32(frame + 4) : mp3_read_be32(frame + 4);

	/* Validate frame size */
	if (frame_size == 0 || frame_size > available_size - 10)
		return 0;

	/* Parse text frames */
	const unsigned char *frame_data = frame + 10;
	if (parse_id3v2_text_frame(frame_id, frame_data, frame_size, meta))
		*found_metadata = true;

	return 10 + frame_size;
}

/*
 * Skip ID3v2 extended header if present
 * Returns new offset after extended header
 */
static size_t skip_id3v2_extended_header(const unsigned char *data, size_t offset, size_t limit,
					 uint8_t version, uint8_t flags) {
	/* Check if extended header flag is set */
	if (!(flags & 0x40) || offset >= limit)
		return offset;

	if (version == 4) {
		/* ID3v2.4 extended header */
		if (offset + 4 > limit)
			return offset;
		uint32_t ext_size = mp3_read_synchsafe32(data + offset);
		if (ext_size > 0 && offset + ext_size <= limit)
			return offset + ext_size;
	} else if (version == 3) {
		/* ID3v2.3 extended header */
		if (offset + 4 > limit)
			return offset;
		uint32_t ext_size = mp3_read_be32(data + offset);
		if (ext_size > 0 && offset + 4 + ext_size <= limit)
			return offset + 4 + ext_size;
	}

	return offset;
}

/*
 * Parse ID3v2 tag and extract metadata
 * Returns true if any metadata was found
 */
static bool mp3_parse_id3v2(const unsigned char *data, size_t size, struct track_metadata *meta) {
	/* Validate input */
	if (!data || !meta || size < 10)
		return false;

	/* Check ID3v2 signature */
	if (memcmp(data, "ID3", 3) != 0)
		return false;

	/* Parse header */
	uint8_t version = data[3];
	uint8_t flags = data[5];
	uint32_t tag_size = mp3_read_synchsafe32(data + 6);

	/* Validate version (support v2.3 and v2.4) */
	if (version < 3 || version > 4) {
		return false;
	}

	/* Calculate tag boundaries */
	size_t offset = 10;
	size_t limit = offset + tag_size;
	if (limit > size)
		limit = size;

	/* Skip extended header if present */
	offset = skip_id3v2_extended_header(data, offset, limit, version, flags);

	/* Parse frames */
	bool found = false;
	while (offset + 10 <= limit) {
		uint32_t frame_total_size =
		    decode_id3v2_frame(data + offset, limit - offset, version, meta, &found);
		if (frame_total_size == 0)
			break;

		offset += frame_total_size;

		/* Prevent infinite loop on corrupted data */
		if (offset <= 10)
			break;
	}

	return found;
}

static bool mp3_parse_id3v1(const unsigned char *data, size_t size, struct track_metadata *meta) {
	if (size < 128)
		return false;
	const struct mp3_id3v1 *tag = (const struct mp3_id3v1 *)(data + size - 128);
	if (tag->id[0] != 'T' || tag->id[1] != 'A' || tag->id[2] != 'G')
		return false;

	bool found = false;

	char *title = mp3_dup_trim_ascii((const unsigned char *)tag->title, sizeof(tag->title));
	metadata_set_if_empty(&meta->title, title);
	if (title && meta->title == title)
		found = true;

	char *artist = mp3_dup_trim_ascii((const unsigned char *)tag->artist, sizeof(tag->artist));
	metadata_set_if_empty(&meta->artist, artist);
	if (artist && meta->artist == artist)
		found = true;

	char *album = mp3_dup_trim_ascii((const unsigned char *)tag->album, sizeof(tag->album));
	metadata_set_if_empty(&meta->album, album);
	if (album && meta->album == album)
		found = true;

	if (meta->track_number < 0 && tag->comment[28] == 0 && tag->comment[29] != 0) {
		meta->track_number = tag->comment[29];
		found = true;
	}

	if (!meta->track && meta->track_number > 0) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", meta->track_number);
		metadata_set_if_empty(&meta->track, strdup(buf));
	}

	return found;
}
/* --------------------------------------------------------------------------- */

bool mp3_info(char *filename, struct tuneinfo *ti) {
	int f;
	unsigned long h;
	int variable_frames;
	unsigned char *pmm_start;
	unsigned char *pmm_end;
	bool is_valid_mp3 = false;
	unsigned char *header;
	int error;
	struct stat ss;
	struct mp3_id3v1 *pv1;

	/*
	 * Open, get the file size and setup the
	 * mp3 file for memory mapped reading.
	 */
	f = open(filename, O_RDONLY);
	if (-1 == f) {
		fprintf(stderr, "\nmp3_read_info: fail in open '%s'\n", filename);
		return false;
	}
	error = fstat(f, &ss);
	if (error) {
		close(f);
		fprintf(stderr, "\nmp3_read_info: fail in stat '%s'\n", filename);
		return false;
	}

	/*
	 * From the excellent http://insights.oetiker.ch/linux/fadvise.html .
	 * Tell the OS NOT to cache the scanned mp3's into memory,
	 * since they are only read once.
	 */
#if defined(POSIX_FADV_DONTNEED)
	fdatasync(f);
	posix_fadvise(f, 0, 0, POSIX_FADV_DONTNEED);
#endif

	ti->filesize = ss.st_size;
	ti->filedate = ss.st_mtime;
	pmm_start = mmap(0, ti->filesize, PROT_READ, MAP_SHARED, f, 0);
	pmm_end = pmm_start + ti->filesize;
	close(f);

	/*
	 * Keep reading 4 bytes from the header until we know
	 * for sure that in fact it's an MP3
	 */
	for (header = pmm_start; header < pmm_end; header++) {
		/*
		 * An mp3 header always starts with 0xff.
		 * Search for it.
		 */
		header = memchr(header, 0xff, pmm_end - header);
		if (!header)
			break;

		h = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | (header[3]);

		if (!mp3_is_valid_header(h))
			continue;

		/*
		 * Read past (the verified valid) header
		 */
		header += 4;

		/*
		 * Is this MPEG Version 1, or MPEG Version 2.0/2.5
		 */
		if (mp3_getVersionIndex(h) == 3)
			header += mp3_getModeIndex(h) == 3 ? 17 : 32;
		else
			header += mp3_getModeIndex(h) == 3 ? 9 : 17;

		/*
		 * If it's a variable bitrate MP3, the first 4 bytes
		 * will read 'Xing' since they're the ones who added
		 * variable bitrate-edness to MP3s.
		 */
		variable_frames = 0;
		if (header[0] == 'X' && header[1] == 'i' && header[2] == 'n' && header[3] == 'g') {
			if (header[7] & 0x01) {
				variable_frames = (header[8] << 24) | (header[9] << 16) |
						  (header[10] << 8) | (header[11]);
			}
		}

		ti->bitrate = mp3_calc_bit_rate(h, ti->filesize, variable_frames);
		ti->duration = mp3_calc_length_in_seconds(ti->filesize, ti->bitrate);

		/*
		 * Get genre from the ID3 tag
		 */
		pv1 = (void *)(pmm_end - 128);
		if (pv1->id[0] == 'T' && pv1->id[1] == 'A' && pv1->id[2] == 'G')
			ti->genre = pv1->genre;
		else
			ti->genre = 0xff;

		is_valid_mp3 = true;
		break;
	}

	munmap(pmm_start, ti->filesize);
	if (!is_valid_mp3 && ti->filesize)
		fprintf(stderr, "\nmp3_read_info: cannot find mp3-info for '%s'\n", filename);
	return is_valid_mp3;
}
bool mp3_metadata(char *filename, struct track_metadata *meta) {
	if (!meta)
		return false;
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
		return false;
	struct stat ss;
	if (fstat(fd, &ss) != 0 || ss.st_size <= 0) {
		close(fd);
		return false;
	}
	unsigned char *mapped = mmap(0, ss.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (mapped == MAP_FAILED)
		return false;
	bool found = false;
	found |= mp3_parse_id3v2(mapped, ss.st_size, meta);
	found |= mp3_parse_id3v1(mapped, ss.st_size, meta);
	munmap(mapped, ss.st_size);
	return found;
}

/* -------------------------------------------------------------------------- */

bool mp3_isit(char *s, int len) {
	if ((len > 4) && (s[len - 4] == '.') && (s[len - 3] == 'M' || s[len - 3] == 'm') &&
	    (s[len - 2] == 'P' || s[len - 2] == 'p') && (s[len - 1] == '3')) {
		return true;
	}

	return false;
}

void mp3_play(char *filename) {
	const char *player = config_get_mp3_player_path();
	execlp(player, player, filename, NULL);

	/*
	 * !!20061123 KB
	 * It seems like mpg321 likes to load up the entire file
	 * before playing it, which causes unnecessary delays over
	 * a network. Try using madplay instead.
	 */
	/*
	 *	execlp("madplay", "madplay", filename, NULL);
	 */
}
