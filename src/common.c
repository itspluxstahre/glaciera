/*
 * common.c - Common stuff for glaciera-indexer and glaciera
 *
 * Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
 * Portions borrowed from the world wild web :)
 *
 * fuzzy() Copyright (c) 1997 Reinhard Rapp  
 * 	http://www.heise.de/ct/english/97/04/386/ 
 *
 * ------------------------------------------------------------------
 *
 * The Gnu General Public License as described below is available
 * in the file COPYING distributed with this package.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glob.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include "common.h"

/* Legacy config variables - will be populated from new config system */
char opt_datapath       [100];
char opt_ripperspath   [100];
char opt_mp3playerpath [100];
char opt_mp3playerflags[100];
char opt_oggplayerpath [100];
char opt_oggplayerflags[100];
char opt_flacplayerpath[100];
char opt_flacplayerflags[100];
char tolowerarray[256];

/**
 * Check if a value is within a specified range (inclusive).
 *
 * @param v The value to check
 * @param min The minimum value of the range
 * @param max The maximum value of the range
 * @return true if v is between min and max (inclusive), false otherwise
 */
bool inrange(int v, int min, int max)
{
        return (v >= min) && (v <= max);
}

/* -------------------------------------------------------------------------- */

char *gethomedir(void)
{
	/*
	 * Use the _standard_ way of getting the users home directory.
	 * The previous, "getpwuid(getuid())->pw_dir" was too strange and unreadable.
	 * Why it didn't work on OSX, is a mystery, since it's a documented function
	 * http://developer.apple.com/documentation/Darwin/Reference/Manpages/man3/getpwuid.3.html 	 
	 *
	 * Plux:
	 * Well who said programming is easy? Apple perhaps does some kind of black magic?
	 * It worked _MOST_ of the time, but sometimes it failed.
	 * Sometimes it is the simple things that screw the most :) 
	 */
	return getenv("HOME");
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
void swap(struct tune **a, struct tune **b)
{
        struct tune *temp;

        temp = *a;    /* Save pointer from a */
        *a = *b;      /* Copy pointer from b to a */
        *b = temp;    /* Copy saved pointer to b */
}

/**
 * Check if a key is a typeable character (letter or digit).
 *
 * @param key The key code to check
 * @return true if the key is alphanumeric, false otherwise
 */
bool is_typeable_key(int key)
{
        return ((key >= 'a') && (key <= 'z')) ||
               ((key >= 'A') && (key <= 'Z')) ||
               ((key >= '0') && (key <= '9'));
/*return isalnum(key);*/
}

static bool istypeablearray[256];
static char toupperarray[256];

/**
 * Initialize fast lookup arrays for character type checking and case conversion.
 * Populates istypeablearray and toupperarray/tolowerarray for efficient character processing.
 */
void build_fastarrays(void)
{
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
void only_searchables(char *src)
{
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
void sanitize_user_input(char *src)
{
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
void chop(char *buf)
{
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
int trim(char s[])
{
        if (!s)
                return 0;

        size_t len = strlen(s);
        if (len == 0)
                return 0;

        /* Find last non-whitespace character */
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r')) {
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
char *strrev(char *str)
{
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
inline static int NGramMatch(char *haystack,
                      char *needle,
                      int needlelen,
                      int NGramLen,
                      int *MaxMatch)
{
        char NGram[8];
        int NGramCount;
        int i, Count;

        NGram[NGramLen] = '\0';
        NGramCount = needlelen - NGramLen + 1;
        *MaxMatch = 0;
        Count = 0;

        /* Suchstring in n-Gramme zerlegen und diese im Text suchen */
        for (i = 0; i < NGramCount; i++) {
                memcpy(NGram, &needle[i], NGramLen);
                *MaxMatch += NGramLen;
                if (strstr(haystack, NGram))
                        Count++;
        }

        return Count * NGramLen;        /* gewichten nach n-Gramm-Laenge */
}

int fuzzy(char *haystack, char *needle)
{
        int needlelen;
        int MatchCount1, MatchCount2;
        int MaxMatch1, MaxMatch2;
        double Similarity;

        /* length of that string */      needlelen = strlen(needle);

        /* call internal function NGramMatch twice */
        MatchCount1 = NGramMatch(haystack, needle, needlelen, 
				 3, &MaxMatch1);
        MatchCount2 = NGramMatch(haystack, needle, needlelen, 
				 (needlelen < 7) ? 2 : 5, &MaxMatch2);

        /* calc hit rate */
        Similarity = 100.0 * (double) (MatchCount1 + MatchCount2) / (double) (MaxMatch1 + MaxMatch2);

        return Similarity;
}

/* -------------------------------------------------------------- */

#define BITSPERWORD     32
#define CALCBYTES(bits) (4 * ((31 + bits) / BITSPERWORD))

/**
 * Set a bit in a bit array.
 *
 * @param abits The bit array to modify
 * @param i The bit index to set (0-based)
 */
inline void bitset (BITS *abits, int i)     {        abits[i / BITSPERWORD] |=  (1<<(i % BITSPERWORD)); }

/**
 * Clear a bit in a bit array.
 *
 * @param abits The bit array to modify
 * @param i The bit index to clear (0-based)
 */
inline void bitclr (BITS *abits, int i)     {        abits[i / BITSPERWORD] &= ~(1<<(i % BITSPERWORD)); }

/**
 * Test if a bit is set in a bit array.
 *
 * @param abits The bit array to test
 * @param i The bit index to test (0-based)
 * @return true if the bit is set, false otherwise
 */
inline bool bittest(BITS *abits, int i)     { return (abits[i / BITSPERWORD] &   (1<<(i % BITSPERWORD))) != 0; }

/**
 * Clear all bits in a bit array.
 *
 * @param abits The bit array to clear
 * @param bits The number of bits in the array
 */
inline void bitnull(BITS *abits, int bits)  {        memset(abits, 0, CALCBYTES(bits));                 }

/**
 * Allocate a new bit array.
 *
 * @param bits The number of bits needed in the array
 * @return Pointer to the allocated bit array, or NULL on failure
 */
inline BITS *bitalloc(int bits)             { return malloc(CALCBYTES(bits));                           }

/* Old config file parsing removed - now using TOML config in config.c */

/* -------------------------------------------------------------------------- */

void urlencode(char *dst, const char *src)
{
	char threechars[4];
	
	while (*src) {
		if (isalnum(*src) || 
		    '.' == *src || 
		    '/' == *src || 
		    ':' == *src) {
			*dst++ = *src;
		} else if (' ' == *src) {
			*dst++ = '+';
		} else {
			snprintf(threechars, sizeof(threechars), "%%%2x", *src);
			*dst++ = threechars[0];
			*dst++ = threechars[1];
			*dst++ = threechars[2];
		}
		src++;
	}
	*dst = '\0';
}

/* -------------------------------------------------------------------------- */


void track_metadata_init(struct track_metadata *meta)
{
	if (!meta)
		return;
	meta->title = NULL;
	meta->artist = NULL;
	meta->album = NULL;
	meta->track = NULL;
	meta->track_number = -1;
}

void track_metadata_clear(struct track_metadata *meta)
{
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


