// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
// Portions borrowed from the world wild web :)
// fuzzy() Copyright (c) 1997 Reinhard Rapp - http://www.heise.de/ct/english/97/04/386/

// System headers
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <wordexp.h>

// Local headers
#include "common.h"

char tolowerarray[256];

/**
 * Check if a value is within a specified range (inclusive).
 *
 * @param v The value to check
 * @param min The minimum value of the range
 * @param max The maximum value of the range
 * @return true if v is between min and max (inclusive), false otherwise
 */
bool inrange(int v, int min, int max) {
	return (v >= min) && (v <= max);
}

/* -------------------------------------------------------------------------- */

/**
 * Swap two tune pointers in an array.
 *
 * This function exchanges the values of two pointers to tune structures.
 * Used for shuffling songs or reordering the display/playlist.
 *
 * Example: If a points to song X and b points to song Y,
 *          after swap(a, b), a points to Y and b points to X.
 *
 * @param a Pointer to first tune pointer (will receive value from b)
 * @param b Pointer to second tune pointer (will receive value from a)
 */
void swap(struct tune **a, struct tune **b) {
	struct tune *temp;

	temp = *a; /* Save pointer from a */
	*a = *b; /* Copy pointer from b to a */
	*b = temp; /* Copy saved pointer to b */
}

/**
 * Check if a key is a typeable character (letter or digit).
 *
 * @param key The key code to check
 * @return true if the key is alphanumeric, false otherwise
 */
bool is_typeable_key(int key) {
	return ((key >= 'a') && (key <= 'z')) || ((key >= 'A') && (key <= 'Z'))
	    || ((key >= '0') && (key <= '9'));
	/*return isalnum(key);*/
}

static bool istypeablearray[256];
static char toupperarray[256];

/**
 * Initialize fast lookup arrays for character type checking and case conversion.
 * Populates istypeablearray and toupperarray/tolowerarray for efficient character processing.
 */
void build_fastarrays(void) {
	int i;

	for (i = 0; i < 256; i++) {
		istypeablearray[i] = is_typeable_key(i);
		toupperarray[i] = toupper(i);
		tolowerarray[i] = tolower(i);
	}
}

/**
 * Filter a string to only include searchable characters (alphanumeric).
 * Converts the string to uppercase for case-insensitive searching.
 *
 * @param src The source string to filter
 */
void only_searchables(char *src) {
	char *dst;

	for (dst = src; *src; src++) {
		if (istypeablearray[*src & 0xff])
			*dst++ = toupperarray[*src & 0xff];
	}
	*dst = '\0';
}

/**
 * Sanitize user input by converting '+' characters to spaces and filtering to typeable characters.
 *
 * @param src The source string to sanitize
 */
void sanitize_user_input(char *src) {
	char *dst;

	for (dst = src; *src; src++) {
		if ('+' == *src)
			*dst++ = ' ';
		else if (istypeablearray[*src & 0xff])
			*dst++ = *src;
	}
	*dst = '\0';
}

/**
 * Remove trailing newline characters from a string buffer.
 * Handles both Unix (\n) and DOS (\r\n) line endings.
 * Commonly used after fgets() to strip the trailing newline.
 *
 * @param buf The string buffer to modify (null-terminated)
 */
void chop(char *buf) {
	if (!buf)
		return;

	size_t len = strlen(buf);
	if (len == 0)
		return;

	/* Remove trailing \n */
	if (buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len--;
	}

	/* Remove trailing \r if present (DOS line endings) */
	if (len > 0 && buf[len - 1] == '\r') {
		buf[len - 1] = '\0';
	}
}

/**
 * Remove trailing whitespace (spaces, tabs, newlines) from a string.
 * Note: This function is currently unused in the codebase.
 *
 * @param s The string to trim (modified in-place)
 * @return The new length of the string after trimming
 */
int trim(char s[]) {
	if (!s)
		return 0;

	size_t len = strlen(s);
	if (len == 0)
		return 0;

	/* Find last non-whitespace character */
	while (len > 0
	    && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n'
		|| s[len - 1] == '\r')) {
		len--;
	}

	/* Null-terminate at new end */
	s[len] = '\0';
	return (int)len;
}

/* -------------------------------------------------------------------------- */

/**
 * Reverse a string in-place
 * @param str String to reverse
 * @return Pointer to the reversed string (same as input)
 */
char *strrev(char *str) {
	char *left, *right;
	char temp;

	if (!str || !*str)
		return str;

	/* Swap characters from both ends moving towards center */
	for (left = str, right = str + strlen(str) - 1; right > left; ++left, --right) {
		temp = *left;
		*left = *right;
		*right = temp;
	}

	return str;
}

/* -------------------------------------------------------------------------- */

/* main function, unmodified from original code */
inline static int NGramMatch(
    char *haystack, char *needle, int needlelen, int NGramLen, int *MaxMatch) {
	char NGram[8];
	int NGramCount;
	int i, Count;

	if (!haystack || !needle || !MaxMatch)
		return 0;
	if (NGramLen <= 0 || NGramLen >= (int)sizeof(NGram))
		return 0;
	if (needlelen < NGramLen)
		return 0;

	NGram[NGramLen] = '\0';
	NGramCount = needlelen - NGramLen + 1;
	*MaxMatch = 0;
	Count = 0;

	/* Suchstring in n-Gramme zerlegen und diese im Text suchen */
	for (i = 0; i < NGramCount; i++) {
		memcpy(NGram, &needle[i], (size_t)NGramLen);
		NGram[NGramLen] = '\0';
		*MaxMatch += NGramLen;
		if (strstr(haystack, NGram))
			Count++;
	}

	return Count * NGramLen; /* gewichten nach n-Gramm-Laenge */
}

int fuzzy(char *haystack, char *needle) {
	int needlelen;
	int MatchCount1, MatchCount2;
	int MaxMatch1, MaxMatch2;
	double Similarity;

	/* length of that string */ needlelen = strlen(needle);

	/* call internal function NGramMatch twice */
	MatchCount1 = NGramMatch(haystack, needle, needlelen, 3, &MaxMatch1);
	MatchCount2 = NGramMatch(haystack, needle, needlelen, (needlelen < 7) ? 2 : 5, &MaxMatch2);

	/* calc hit rate */
	Similarity = 100.0 * (double)(MatchCount1 + MatchCount2) / (double)(MaxMatch1 + MaxMatch2);

	return Similarity;
}

/* -------------------------------------------------------------- */

#define BITSPERWORD 32
#define CALCBYTES(bits) (4 * ((31 + bits) / BITSPERWORD))

/**
 * Set a bit in a bit array.
 *
 * @param abits The bit array to modify
 * @param i The bit index to set (0-based)
 */
inline void bitset(BITS *abits, int i) {
	abits[i / BITSPERWORD] |= (1 << (i % BITSPERWORD));
}

/**
 * Clear a bit in a bit array.
 *
 * @param abits The bit array to modify
 * @param i The bit index to clear (0-based)
 */
inline void bitclr(BITS *abits, int i) {
	abits[i / BITSPERWORD] &= ~(1 << (i % BITSPERWORD));
}

/**
 * Test if a bit is set in a bit array.
 *
 * @param abits The bit array to test
 * @param i The bit index to test (0-based)
 * @return true if the bit is set, false otherwise
 */
inline bool bittest(BITS *abits, int i) {
	return (abits[i / BITSPERWORD] & (1 << (i % BITSPERWORD))) != 0;
}

/**
 * Clear all bits in a bit array.
 *
 * @param abits The bit array to clear
 * @param bits The number of bits in the array
 */
inline void bitnull(BITS *abits, int bits) {
	memset(abits, 0, CALCBYTES(bits));
}

/**
 * Allocate a new bit array.
 *
 * @param bits The number of bits needed in the array
 * @return Pointer to the allocated bit array, or NULL on failure
 */
inline BITS *bitalloc(int bits) {
	return malloc(CALCBYTES(bits));
}

/* Old config file parsing removed - now using TOML config in config.c */
/* Old FCGI web interface removed - no longer needed */

/* -------------------------------------------------------------------------- */

void track_metadata_init(struct track_metadata *meta) {
	if (!meta)
		return;
	meta->title = NULL;
	meta->artist = NULL;
	meta->album = NULL;
	meta->track = NULL;
	meta->track_number = -1;
}

void track_metadata_clear(struct track_metadata *meta) {
	if (!meta)
		return;
	free(meta->title);
	free(meta->artist);
	free(meta->album);
	free(meta->track);
	meta->title = NULL;
	meta->artist = NULL;
	meta->album = NULL;
	meta->track = NULL;
	meta->track_number = -1;
}

/* -------------------------------------------------------------------------- */
/* Safe String Handling Functions                                             */
/* -------------------------------------------------------------------------- */

/**
 * Safe string copy with bounds checking
 * Returns the length of the copied string
 */
size_t safe_strcpy(char *dst, const char *src, size_t dst_size) {
	if (!dst || !src || dst_size == 0)
		return 0;

	size_t src_len = strlen(src);
	size_t copy_len = (src_len < dst_size - 1) ? src_len : (dst_size - 1);

	memcpy(dst, src, copy_len);
	dst[copy_len] = '\0';

	return copy_len;
}

/**
 * Safe string concatenation with bounds checking
 * Returns the total length of the resulting string
 */
size_t safe_strcat(char *dst, const char *src, size_t dst_size) {
	if (!dst || !src || dst_size == 0)
		return 0;

	size_t dst_len = strnlen(dst, dst_size);
	if (dst_len >= dst_size - 1)
		return dst_len; /* No space left */

	size_t remaining = dst_size - dst_len - 1;
	size_t src_len = strlen(src);
	size_t copy_len = (src_len < remaining) ? src_len : remaining;

	memcpy(dst + dst_len, src, copy_len);
	dst[dst_len + copy_len] = '\0';

	return dst_len + copy_len;
}

/**
 * Safe path joining with automatic separator handling
 * Returns true on success, false if buffer too small
 */
static bool path_contains_parent_reference(const char *path) {
	if (!path)
		return false;

	size_t i = 0;
	while (path[i] != '\0') {
		while (path[i] == '/')
			i++;
		size_t start = i;
		while (path[i] != '/' && path[i] != '\0')
			i++;
		size_t len = i - start;
		if (len == 2 && path[start] == '.' && path[start + 1] == '.')
			return true;
	}

	return false;
}

bool path_is_secure(const char *path) {
	if (!path || path[0] == '\0')
		return false;
	if (path[0] != '/')
		return false;
	if (path_contains_parent_reference(path))
		return false;
	return true;
}

bool env_path_copy_if_safe(const char *name, char *dst, size_t dst_size) {
	if (!name || !dst || dst_size == 0)
		return false;

	const char *value = getenv(name);
	if (!value || value[0] == '\0')
		return false;
	if (!path_is_secure(value))
		return false;

	safe_strcpy(dst, value, dst_size);
	return true;
}

bool safe_path_join(char *dst, size_t dst_size, const char *path1, const char *path2) {
	if (!dst || !path1 || !path2 || dst_size == 0)
		return false;
	if (path2[0] == '/')
		return false;
	if (path_contains_parent_reference(path2))
		return false;

	size_t len1 = strlen(path1);
	size_t len2 = strlen(path2);
	bool needs_sep = (len1 > 0 && path1[len1 - 1] != '/' && path2[0] != '/');

	size_t total_len = len1 + len2 + (needs_sep ? 1 : 0);
	if (total_len >= dst_size)
		return false;

	safe_strcpy(dst, path1, dst_size);
	if (needs_sep)
		safe_strcat(dst, "/", dst_size);
	safe_strcat(dst, path2, dst_size);

	return true;
}

/**
 * Safe getenv with default value and validation
 * Returns environment variable value or default if not set/empty
 */
char *safe_getenv(const char *name, const char *default_value) {
	if (!name)
		return (char *)default_value;

	char *value = getenv(name);
	if (!value || value[0] == '\0')
		return (char *)default_value;

	return value;
}

bool player_exec(const char *player, const char *flags, const char *const *extra_args,
    size_t extra_count, const char *filename) {
	if (!player || player[0] == '\0') {
		fprintf(stderr, "Error: No player command configured.\n");
		return false;
	}

	wordexp_t we;
	memset(&we, 0, sizeof(we));
	bool have_flags = false;
	size_t flag_count = 0;

	if (flags && *flags) {
		int wret = wordexp(flags, &we, WRDE_NOCMD | WRDE_SHOWERR);
		if (wret == 0) {
			have_flags = true;
			flag_count = (size_t)we.we_wordc;
		} else {
			const char *reason = "invalid syntax";
			switch (wret) {
			case WRDE_BADCHAR:
				reason = "invalid character in flags";
				break;
			case WRDE_BADVAL:
				reason = "undefined variable in flags";
				break;
			case WRDE_CMDSUB:
				reason = "command substitution is not allowed";
				break;
			case WRDE_NOSPACE:
				reason = "out of memory while parsing flags";
				wordfree(&we);
				break;
			case WRDE_SYNTAX:
				reason = "syntax error in flags";
				break;
			default:
				break;
			}
			fprintf(
			    stderr, "Warning: Ignoring player flags \"%s\": %s.\n", flags, reason);
		}
	}

	size_t filename_count = filename ? 1 : 0;
	size_t total_args = 1 + flag_count + extra_count + filename_count;
	char **argv = calloc(total_args + 1, sizeof(char *));
	if (!argv) {
		fprintf(
		    stderr, "Error: Unable to allocate argument vector for player %s.\n", player);
		if (have_flags)
			wordfree(&we);
		return false;
	}

	size_t idx = 0;
	argv[idx++] = (char *)player;

	if (have_flags) {
		for (size_t i = 0; i < flag_count; i++)
			argv[idx++] = we.we_wordv[i];
	}

	for (size_t i = 0; i < extra_count; i++)
		argv[idx++] = (char *)extra_args[i];

	if (filename)
		argv[idx++] = (char *)filename;
	argv[idx] = NULL;

	execvp(player, argv);

	int saved_errno = errno;
	fprintf(stderr, "Error executing player '%s': %s\n", player, strerror(saved_errno));

	if (have_flags)
		wordfree(&we);
	free(argv);
	errno = saved_errno;
	return false;
}
