#define noDEBUG
/*
 * glaciera-indexer
 *      generates the database for glaciera
 *
 * Goal 1: Recursive scan of all audio files
 *      Takes ~5 minutes on my old P120 40MB RedHat9 machine
 *      to scan ~71000 audio files spread over ten hard disks.
 *
 * Goal 2: Calculate the song length for every audio file.
 *      Takes an extra 35 minutes on above machine.
 *      This is what I call a FullScan.
 *
 * Goal 3: Make it fast!
 *      Now takes a total of ~5 1/2 minutes to rescan files
 *      when the database file has been built once.
 *      This is what I call a QuickScan.
 *
 * Goal 4: Make it even faster!
 *      For multi-disk systems, read each disk in its own thread,
 *      scanning only the if the contents have changed since the
 *      last scan. This is what I call a TurboScan.
 *
 * 2005-05-28:
 *	System: P200 96MB FC3 machine 185000 audio files 8 physical disks
 *      Turboscan:  ~1 minute (1 disk rescan)
 *      Quickscan:  ~4 minutes
 *      Fullscan:  ~60 minutes
 *
 * Copyright (c) 2003-2010 Krister Brus <kristerbrus@fastmail.fm>
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

/*
 * http://www.suse.de/~aj/linux_lfs.html
 */
#define _LARGEFILE_SOURCE 
#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <pthread.h>
#include <limits.h>
#include <sqlite3.h>
#include "common.h"
#include "config.h"
#include "db.h"
#include "git_version.h"
#include "music.h"

static char *massage_full_path(char *buf, char *fullpath);

struct smalltune *smalltunes = NULL;
int allcount = 0;
int total_files = 0;
double total_bytes = 0;
int new_files = 0;
time_t timeprogress = 0;
bool opt_generate_allmp3db = false;
bool opt_force_build = false;
bool opt_skip_file_info = false;

pthread_mutex_t filemutex = PTHREAD_MUTEX_INITIALIZER;

/* --------------------------------------------------------------------------- */

struct ripvars {
        char *str;
        int len;
};

struct {
        short lo;
        short hi;
} qsearch[256];

struct ripvars *rippers = NULL;
int ripperscount = 0;

int rip_sort_function(const void *a, const void *b)
{
        return strcasecmp(((struct ripvars *) a)->str,
                          ((struct ripvars *) b)->str);
}

void load_rippers(char *filename)
{
        FILE *f;
        char buf[255];
        int i;
        int ch;

        f = fopen(filename, "r");
        if (!f)
                return;

        ripperscount = 0;
        while (fgets(buf, sizeof(buf), f))
                ripperscount++;
        rewind(f);
        rippers = malloc(ripperscount * sizeof(struct ripvars));
        ripperscount = 0;
        while (fgets(buf, sizeof(buf), f)) {
                chop(buf);
                if (buf[0]) {
                        rippers[ripperscount].str = strdup(buf);
                        rippers[ripperscount].len = strlen(buf);
                        ripperscount++;
                }
        }
        fclose(f);

        for(i = 0; i < ripperscount; i++)
                strrev(rippers[i].str);
        qsort((void *) rippers, ripperscount, sizeof(struct ripvars), rip_sort_function);
        for(i = 0; i < ripperscount; i++)
                strrev(rippers[i].str);
        memset(qsearch, 0, sizeof(qsearch));
        for (i = 0; i < ripperscount; i++) {
                ch = toupper(0xff & rippers[i].str[rippers[i].len-1]);
                if (qsearch[ch].lo == 0)
                        qsearch[ch].lo = i;
                qsearch[ch].hi = i;
        }
}

/*
 * Strip the trailing XXXX from the string AAAAAXXXX.
 * The XXXX strings are stored in the rippers array
 */
void strip_ripper(char *s)
{
        int i;
        char *p;
        int slen = strlen(s);
        int ch;

        ch = toupper(0xff & s[slen-1]);
#ifdef BRUTE_FORCE
        for (i = 0; i < ripperscount; i++) {
#else
        for (i = qsearch[ch].lo; i <= qsearch[ch].hi; i++) {
#endif
                if (i) {
                        p = s + slen - rippers[i].len;
                        if (p > s && 0 == strncasecmp(p, rippers[i].str, rippers[i].len)) {
                                *p = 0;
                                break;
                        }
                }
        }
}

/* --------------------------------------------------------------------------- */

void get_cached_info(char *filename, struct tuneinfo *ti)
{
        struct db_track *track = db_get_track_by_filepath(filename);

        if (track) {
                memcpy(ti, &track->ti, sizeof(struct tuneinfo));
                db_free_track(track);
        } else if (!opt_skip_file_info) {
                music_info(filename, ti);
                /* Note: new_files is incremented when inserting into DB, not here */
        }

        total_files++;
        total_bytes += ti->filesize;
}

/* --------------------------------------------------------------------------- */

void trim_display_path(char *src)
{
        char *dst;

        dst = src;
        while (*src) {
                switch (*src) {
                case '_': *dst++ = ' ';  break;
                case '[': *dst++ = '(';  break;
                case ']': *dst++ = ')';  break;
                default : *dst++ = *src; break;
                }
                src++;
        }
}

void trim_double_spaces(char *src)
{
        char *dst;

        dst = src;
        while (*src) {
                while (' ' == *src && ' ' == *(src+1))
                        src++;
                *dst++ = *src++;
        }
        *dst = 0;
}

void trim_double_minuses(char *src)
{
        char *dst;

        dst = src;
        while (*src) {
                while ('-' == *src && '-' == *(src+1))
                        src++;
                *dst++ = *src++;
        }
        *dst = 0;
}

void trim_minus_space_minus(char *src)
{
        char *dst;

        dst = src;
        while (*src) {
                if (('-' == *(src+0) || '.' == *(src+0)) &&
                     ' ' == *(src+1) &&
                     '-' == *(src+2)) {
                        *dst++ = '.';
                        src++;
                        src++;
                        src++;
		} else
			*dst++ = *src++;
        }
        *dst = 0;
}

void trim_space_dot_space(char *src)
{
        char *dst;

        dst = src;
        while (*src) {
                if ((' ' == *(src+0)) &&
                     '.' == *(src+1) &&
                     ' ' == *(src+2)) {
                        *dst++ = '.';
                        *dst++ = ' ';
                        src++;
                        src++;
                        src++;
		} else
			*dst++ = *src++;
        }
        *dst = 0;
}

/*
 * Get the length of a UTF-8 character sequence starting at the given byte
 * Returns 1-4 for valid sequences, 1 for invalid bytes
 */
static int utf8_char_len(unsigned char c)
{
        if ((c & 0x80) == 0x00) return 1;      /* 0xxxxxxx - ASCII */
        if ((c & 0xE0) == 0xC0) return 2;      /* 110xxxxx - 2-byte */
        if ((c & 0xF0) == 0xE0) return 3;      /* 1110xxxx - 3-byte */
        if ((c & 0xF8) == 0xF0) return 4;      /* 11110xxx - 4-byte */
        return 1;  /* Invalid UTF-8, treat as single byte */
}

static void build_display_from_filename(const char *dir,
                                        const char *filename,
                                        BITS keepers[],
                                        char *out,
                                        size_t out_size)
{
        char fullpath[1024 * 4];
        char gbuf[1024 * 4];
        size_t dir_len = strlen(dir);

        if (dir_len >= sizeof(fullpath)) {
                snprintf(out, out_size, "%s", filename);
                return;
        }

        strncpy(fullpath, dir, sizeof(fullpath));
        fullpath[sizeof(fullpath) - 1] = '\0';
        trim_display_path(fullpath);
        strip_ripper(fullpath);
        strncat(fullpath, "/", sizeof(fullpath) - strlen(fullpath) - 1);

        /* UTF-8 aware copying using keepers bitmap */
        char *p = fullpath + strlen(fullpath);
        for (int i = 0; filename[i] && (size_t)(p - fullpath) < sizeof(fullpath) - 1; ) {
                /* Determine UTF-8 character length */
                int char_len = utf8_char_len((unsigned char)filename[i]);
                
                /* Check if first byte of character should be kept */
                if (bittest(keepers, i)) {
                        /* Copy entire UTF-8 character */
                        for (int j = 0; j < char_len && filename[i + j] &&
                             (size_t)(p - fullpath) < sizeof(fullpath) - 1; j++) {
                                *p++ = filename[i + j];
                        }
                }
                
                /* Advance by full character length */
                i += char_len;
        }
        *p = '\0';

        strcpy(gbuf, massage_full_path(gbuf, fullpath));
        trim_display_path(gbuf);
        strip_ripper(gbuf);
        trim_double_spaces(gbuf);
        trim_double_minuses(gbuf);
        trim_minus_space_minus(gbuf);
        trim_space_dot_space(gbuf);

        /* Skip leading non-alphanumeric ASCII, but preserve Unicode */
        p = gbuf;
        while (*p && (unsigned char)*p < 128 && !isalnum((unsigned char)*p))
                p++;

        snprintf(out, out_size, "%s", p);
}

static void build_display_from_metadata(const struct track_metadata *meta,
                                        char *out,
                                        size_t out_size)
{
        out[0] = '\0';
        if (!meta)
                return;

        /* Format: <artist> - <album> - <tracknum> <title> */
        if (meta->artist && meta->album && meta->title) {
                if (meta->track_number > 0)
                        snprintf(out, out_size, "%s - %s - %02d %s", 
                                meta->artist, meta->album, meta->track_number, meta->title);
                else
                        snprintf(out, out_size, "%s - %s - %s", 
                                meta->artist, meta->album, meta->title);
        }
        /* Fallback formats when some metadata is missing */
        else if (meta->artist && meta->title) {
                if (meta->track_number > 0)
                        snprintf(out, out_size, "%s - %02d %s", 
                                meta->artist, meta->track_number, meta->title);
                else
                        snprintf(out, out_size, "%s - %s", meta->artist, meta->title);
        }
        else if (meta->album && meta->title) {
                if (meta->track_number > 0)
                        snprintf(out, out_size, "%s - %02d %s", 
                                meta->album, meta->track_number, meta->title);
                else
                        snprintf(out, out_size, "%s - %s", meta->album, meta->title);
        }
        else if (meta->title) {
                if (meta->track_number > 0)
                        snprintf(out, out_size, "%02d %s", meta->track_number, meta->title);
                else
                        snprintf(out, out_size, "%s", meta->title);
        }
        else if (meta->artist && meta->album) {
                snprintf(out, out_size, "%s - %s", meta->artist, meta->album);
        }
        else if (meta->artist) {
                snprintf(out, out_size, "%s", meta->artist);
        }
        else if (meta->album) {
                snprintf(out, out_size, "%s", meta->album);
        }
        else if (meta->track) {
                snprintf(out, out_size, "%s", meta->track);
        }

        trim_double_spaces(out);
        trim_double_minuses(out);
        trim_minus_space_minus(out);
        trim_space_dot_space(out);
}


char *fix_01_to_fullname(int offset, char *s)
{
        int i;
        int slashcnt;

        slashcnt = 0;
        for (i = strlen(s); i; i--) {
                if (s[i] == '/') {
                        slashcnt++;
                        if (offset == slashcnt) {
                                return s + i + 1;
			}
                }
        }

        return s;
}

char *massage_full_path(char *buf, char *fullpath)
{
        char *p, *r;

        strcpy(buf, fullpath);

        /*
        * Find the actual filename
        */
        r = strrchr(buf, '/');
        if (r)
                r++;
        else	
		r = buf;

        /*
        * "songtitle"    => "path - songtitle"
        * "10.Songtitle" => "path - 10.songtitle"
        */
        if ((NULL == strchr(r, '-') || (isdigit(r[0]) && isdigit(r[1])))) {
                r = fix_01_to_fullname(2, fullpath);
        }

        if ((r[0] == 'c' || r[0] == 'C') &&
            (r[1] == 'd' || r[1] == 'D') &&
            (r[2] == ' ' || isdigit(r[2])) &&
            (r[3] == '-' || r[3] == ' ' || r[3] == '/' || isdigit(r[3]))) {
                r = fix_01_to_fullname(3, fullpath);
        }

        /*
         * Strip the .mp3 / .ogg part
         */
        p = strrchr(r, '.');
        if (p) 
                *p = 0;

        return r;
}

/* --------------------------------------------------------------------------- */

void report_scanning_progress(int sig)
{
        (void)sig;

        static int prev_total_files = 0;
        static double prev_total_bytes = 0;
	int megdiff;
	char suffix[3] = "MB";

	megdiff = (total_bytes - prev_total_bytes) / 1024 / 1024;
	if (megdiff > 1024) {
		megdiff /= 1024;
		strcpy(suffix, "GB");
	}
		
	fprintf(stderr, "\rTotal files: %8d  new files: %8d (%5d/sec %6d%s/sec)",
			total_files, new_files, 
			total_files - prev_total_files,
			megdiff,
			suffix);
	fflush(stderr);
	prev_total_files = total_files;
	prev_total_bytes = total_bytes;

	alarm(1);
}

/* ------------------------------------------------------------------------- */

void process_one_file(char *dir,
                      char *afullpath,
                      char *filename,
                      struct tuneinfo *pfti,
                      BITS keepers[])
{
        char display[1024 * 4];
        char search_text[1024 * 4];
        struct track_metadata meta;
        track_metadata_init(&meta);
        bool have_meta = music_metadata(afullpath, &meta);

        if (have_meta)
                build_display_from_metadata(&meta, display, sizeof(display));

        if (!have_meta || display[0] == '\0')
                build_display_from_filename(dir, filename, keepers, display, sizeof(display));

        if (display[0] == '\0')
                snprintf(display, sizeof(display), "%s", filename);

        char *trimmed = display;
        while (*trimmed && !isalnum((unsigned char)*trimmed))
                trimmed++;

        /* Create search text from display name */
        strcpy(search_text, trimmed);
        only_searchables(search_text);

        /* Check if track already exists and update or insert */
        if (db_track_exists(afullpath)) {
                struct db_track *existing = db_get_track_by_filepath(afullpath);
                if (existing) {
                        /* Update existing track */
                        db_update_track(existing->id, afullpath, trimmed, search_text, pfti);
                        db_free_track(existing);
                }
        } else {
                /* Insert new track */
                db_insert_track(afullpath, trimmed, search_text, pfti);
                new_files++;
        }

        track_metadata_clear(&meta);
}


/*
 * Analyze filename patterns across directory to find common/unique characters
 */
static int analyze_filename_patterns(DIR *pdir, char basefilename[256], 
                                     int samecolumn[256], int sumcolumn[256], 
                                     int trackcolumn[256])
{
        struct dirent *sd;
        char *p;
        int i;
        int musicfiles = 0;

        while (NULL != (sd = readdir(pdir))) {
                if (!music_isit(sd->d_name))
                        continue;

                musicfiles++;
		
		/* Chop the file extension */
	        p = strrchr(sd->d_name, '.');
	       	if (p)
        	       	*p = 0;
                
		if (!basefilename[0])
                        strcpy(basefilename, sd->d_name);

                /* Analyze each character position */
                for (p = sd->d_name, i = 0; *p; p++, i++) {
                        if (' ' == *p || ispunct(*p))
                                continue;
                        if (basefilename[i] == *p)
                                samecolumn[i]++;
                        if (isdigit(*p)) {
                                trackcolumn[i]++;
				sumcolumn[i] += *p;
			}
                }
        }
        
        return musicfiles;
}

/*
 * Convert column counts to boolean flags based on number of files
 */
static void normalize_column_stats(int musicfiles, int samecolumn[256], int trackcolumn[256])
{
        for (int i = 0; i < 256; i++) {
                if (samecolumn[i] != musicfiles)
                        samecolumn[i] = false;
                if (trackcolumn[i] != musicfiles)
                        trackcolumn[i] = false;
	}
}

/*
 * Remove redundant text that appears with track numbers
 * e.g., "cd1" in "cd1-01-song", "cd1-02-song"
 */
static void remove_redundant_with_tracknums(int musicfiles, const char basefilename[256],
                                            int samecolumn[256], int sumcolumn[256],
                                            int trackcolumn[256])
{
        for (int i = 0; i < 256; i++) {
		if (trackcolumn[i]) {
			sumcolumn[i] /= musicfiles;
			if (sumcolumn[i] == basefilename[i] && samecolumn[i]) {
				samecolumn[i] = true;
				trackcolumn[i] = false;
			}
		}
	}
}

/*
 * Find where track numbers start and clean prefix
 */
static void find_and_clean_track_prefix(int samecolumn[256], int trackcolumn[256])
{
	int trackstarts = -1;
        
        /* Find leftmost track number column */
        for (int i = 255; i; i--) {
		while (i>=0 && trackcolumn[i]) {
		 	trackstarts = i;
			i--;
		}
		if (trackstarts != -1)
			 break;
	}
        
        /* Clear everything before track numbers */
        for (int i = 0; i < trackstarts; i++) {
		trackcolumn[i] = false;
		samecolumn[i] = true;
	}
}

/*
 * Handle edge case where leading 0 is removed (e.g., "1" vs "01")
 */
static void fix_missing_leading_zero(int trackcolumn[256])
{
	int trackcount = 0;
        
	for (int i = 0; i < 256; i++)
		trackcount += trackcolumn[i] ? 1 : 0;
        
	if (1 == trackcount) {
		for (int i = 1; i < 256; i++)
			if (trackcolumn[i])
				trackcolumn[i-1] = true;
	}
}

/*
 * Build final keepers bitmap from analysis
 */
static void build_keepers_bitmap(const int samecolumn[256], const int trackcolumn[256], BITS keepers[])
{
	/* Check if we have any track numbers */
	bool justnames = true;
        for (int i = 0; i < 256; i++) {
		if (trackcolumn[i])
			justnames = false;
	}
        
	/* If no track numbers, keep all differences */
	if (justnames) {
	        for (int i = 0; i < 256; i++) 
			if (!samecolumn[i])
				bitset(keepers, i);
		return;
	}

	/* Keep unique chars and track number columns */
	for (int i = 0; i < 256; i++) {
		if (!samecolumn[i])
			bitset(keepers, i);
		if (trackcolumn[i])
			bitset(keepers, i);
	}
}

/*
 * Find duplicate titles for this directory
 * "01. Band - One"
 * "02. Band - Two"
 * "03. Band - Three"
 *
 * Removes redundant parts like "Band" that appear in all files
 */
void find_redundant_song_names(DIR *pdir, BITS keepers[])
{
        char basefilename[256];
        int samecolumn[256];
        int sumcolumn[256];
	int trackcolumn[256];
        int musicfiles;

        memset(basefilename, 0, sizeof(basefilename));
        memset(samecolumn, 0, sizeof(samecolumn));
        memset(trackcolumn, 0, sizeof(trackcolumn));
        memset(sumcolumn, 0, sizeof(sumcolumn));

        /* Analyze all music files in directory */
        musicfiles = analyze_filename_patterns(pdir, basefilename, samecolumn, 
                                               sumcolumn, trackcolumn);

	/* Keep all characters if there's only one file */
        if (musicfiles <= 1) {
		for (int i = 0; i < 256; i++) 
			bitset(keepers, i);
		return;
	}

        /* Normalize statistics */
        normalize_column_stats(musicfiles, samecolumn, trackcolumn);
        
        /* Remove redundant text around track numbers */
        remove_redundant_with_tracknums(musicfiles, basefilename, samecolumn, 
                                        sumcolumn, trackcolumn);
        
        /* Find and clean prefixes before track numbers */
        find_and_clean_track_prefix(samecolumn, trackcolumn);
        
        /* Handle missing leading zeros */
        fix_missing_leading_zero(trackcolumn);
        
        /* Build final result */
        build_keepers_bitmap(samecolumn, trackcolumn, keepers);
}

void * prim_recurse_disc(void *argdir)
{
        DIR *pdir;
#if defined(__APPLE__)
        struct dirent *sd;
#else
        struct dirent64 *sd;
        struct dirent64 *sdbuf;
#endif
        struct stat ss;
        char *fullpath;
        char *dir = argdir;
        struct tuneinfo ti;
        int dirlen = 0;
	BITS keepers[8];
// https://patchwork.kernel.org/patch/110690/

        pdir = opendir(dir);
        if (!pdir)
                return NULL;
		
	memset(keepers, 0, sizeof(keepers));
        find_redundant_song_names(pdir, keepers);
                    
        rewinddir(pdir);

#if defined(__APPLE__)
        while ((sd = readdir(pdir)) != NULL) {
#else
	sdbuf = (struct dirent64 *) malloc(offsetof(struct dirent64, d_name) + NAME_MAX + 1);
	while (readdir64_r(pdir, sdbuf, &sd) == 0 && sd) {
#endif
                /*
                 * Do not even consider directories or files starting with .
                 */
                if ('.' == sd->d_name[0])
                        continue;

		if (!dirlen)
			dirlen = strlen(dir);
                fullpath = malloc(dirlen + 1 + strlen(sd->d_name) + 1);

		/* TODO: Optimize using running pointers 
		 * 	this is fast maybe?
     		 *	     sprintf(fullpath, "%s/%s", dir, sd->d_name);
		 *	also optimize the stat() call from
		 *	the article ....
		 * http://udrepper.livejournal.com/18555.html
		 */
                strncpy(fullpath, dir, dirlen+1);
                strcat(fullpath, "/");
                strcat(fullpath, sd->d_name);

                if (music_isit(sd->d_name)) {
                        memset(&ti, 0, sizeof(ti));
                        get_cached_info(fullpath, &ti);

			pthread_mutex_lock(&filemutex);
			process_one_file(dir, fullpath, sd->d_name, &ti, keepers);
			pthread_mutex_unlock(&filemutex);
//                } else if (DT_DIR == sd->d_type) {
		} else if (0 == stat(fullpath, &ss) && S_ISDIR(ss.st_mode)) {
                        prim_recurse_disc(fullpath);
                }

                free(fullpath);
        }
        closedir(pdir);
#if !defined(__APPLE__)
	free(sdbuf);
#endif
        return NULL;
}

/* --------------------------------------------------------------------------- */

pthread_t threads[100];
int threadcount = 0;

bool is_path_in_mounts(char *path)
{
        FILE *f;
        char buf[1024];
        bool hasmount = false;

        f = fopen("/proc/mounts", "r");
        if (f) {
                while (fgets(buf, sizeof(buf), f)) {
                        if (strstr(buf, path))
                                hasmount = true;
                }
                fclose(f);
        }
        return hasmount;
}

bool path_has_files(char *path)
{
	DIR *pdir;
	struct dirent *sd;
	bool result = false;

	pdir = opendir(path);
	if (pdir) {
		while (NULL != (sd = readdir(pdir))) {
			if (strcmp(sd->d_name, ".") != 0 &&
			    strcmp(sd->d_name, "..") != 0) {
				result = true;
				break;
			}
		}
		closedir(pdir);
	}
	return result;
}

void start_recurse_disc(const char * argdir)
{
        struct statvfs df;
        FILE * f;
        char * dir;
        char buf[1024];
        char freefilename[1024];
        char * p;
        unsigned long stored_free = ULONG_MAX;
	int err;
	bool use_cache = false;

	/*
	 * If the directory ends with a /, remove it
	 * This simplifies the prim_recurse_disc() function
	 */
        dir = strdup(argdir);
	if ('/' == dir[strlen(dir) - 1])
		dir[strlen(dir) - 1] = 0;		
		
        strcpy(buf, dir);
        for (p = buf; *p; p++)
                if ('/' == *p)
                        *p = '_';
	snprintf(freefilename, sizeof(freefilename), "%s%s.free", opt_datapath, buf);

        f = fopen(freefilename, "r");
        if (f) {
                fscanf(f, "%lu", &stored_free);
                fclose(f);
        }

	if (opt_force_build)
		stored_free = ULONG_MAX;

	/*
 	 * NEW FEATURE!!! I call it "ElephantMemory".
 	 *
 	 * Figure how to determine if this path points to a previously mounted, 
 	 * but as of now is an unmounted directory.
 	 * Why? If it does, DO NOT throw away the already stored file 
 	 * information. Instead, just reload it into the new *.db files. 
 	 * But why? Some users, ....,  don't keep all of their servers running 
 	 * all of the time. 
 	 * This is probably the best idea I've had since the introduction of 
 	 * the memory mapped files. This is gonna save us boatloads of
 	 * unnecessary filesystem scanning time. 
 	 */
        err = statvfs(dir, &df);
	if (err)
		df.f_bfree = 0;
	if (is_path_in_mounts(dir))
        	use_cache = df.f_bfree == stored_free;
	else
		use_cache = stored_free != ULONG_MAX;

        if (use_cache) {
                fprintf(stderr, "\nJust parsing mp3-files '%s'...", dir);
		fflush(stderr);

                /* For now, just fall back to full scan */
                pthread_create(&threads[threadcount++],
                                NULL,
                                &prim_recurse_disc,
                                dir);
        } else {
                fprintf(stderr, "\nBrowsing for mp3-files in '%s'...", dir);
		fflush(stderr);

                pthread_create(&threads[threadcount++],
                                NULL,
                                &prim_recurse_disc,
                                dir);

                f = fopen(freefilename, "w");
                if (f) {
                        fprintf(f, "%lu\n", (unsigned long) df.f_bfree);
                        fclose(f);
                }
        }
}

/* --------------------------------------------------------------------------- */

bool can_create_database(char * dir)
{
	char buf[100];
	FILE *f;
	bool writeable = false;

	/*
	 * When the /mp3 directory is shared via samba, this is the only
	 * bullet-proof way to see if the path is read-only.
	 * It's *not* enough to check with access(W_OK) 
	 */
	snprintf(buf, sizeof(buf), "%sjusttesting.deleteme", dir);
	f = fopen(buf, "w");
	if (f) {
		fclose(f);
		unlink(buf);
		writeable = true;
	}
	
	return writeable;
}

/* --------------------------------------------------------------------------- */

void print_version(void)
{
        fprintf(stderr, "Database builder for GLACIERA - %s - %s\n",
			complete_version(),
			__DATE__ " " __TIME__);
        fprintf(stderr, "Copyright (c) Plux Stahre 2025\n");
}

/* --------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
        int i;
	int arg;

        while ((arg = getopt(argc, argv, "hvwfs")) > -1) {
                switch (arg) {
		case 'w':
			opt_generate_allmp3db = true;
			break;
		case 'f':
			opt_force_build = true;
			break;
		case 's':
			opt_skip_file_info = true;
			break;
                case 'h':
                case '?':
                        print_version();
                        printf("usage: glaciera-indexer [-h] [-w] [-f] [-s]\n");
                        printf("options:\n");
                        printf("        -w      Generate allmp3.db for the Windows client\n");
                        printf("        -f      Force parsing (disable TurboScan)\n");
                        printf("        -s      Skip song length calculations\n");
                        exit(0);
                        break;
                case 'v':
                        print_version();
                        exit(0);
                }
        }

        print_version();

	/* Initialize new XDG-compliant configuration system */
	if (!config_init()) {
		fprintf(stderr, "Failed to initialize configuration\n");
		exit(EXIT_FAILURE);
	}

	/* Populate legacy variables from new config */
	strncpy(opt_datapath, xdg_data_dir, sizeof(opt_datapath) - 1);
	strcat(opt_datapath, "/");
	strncpy(opt_ripperspath, global_config.rippers_path, sizeof(opt_ripperspath) - 1);
	strncpy(opt_mp3playerpath, global_config.mp3_player_path, sizeof(opt_mp3playerpath) - 1);
	strncpy(opt_mp3playerflags, global_config.mp3_player_flags, sizeof(opt_mp3playerflags) - 1);
	strncpy(opt_oggplayerpath, global_config.ogg_player_path, sizeof(opt_oggplayerpath) - 1);
	strncpy(opt_oggplayerflags, global_config.ogg_player_flags, sizeof(opt_oggplayerflags) - 1);
	strncpy(opt_flacplayerpath, global_config.flac_player_path, sizeof(opt_flacplayerpath) - 1);
	strncpy(opt_flacplayerflags, global_config.flac_player_flags, sizeof(opt_flacplayerflags) - 1);

	music_register_all_modules();
        build_fastarrays();

	if (!can_create_database(opt_datapath)) {
		fprintf(stderr, "Error: The path '%s' must be writeable.\n", opt_datapath);
		exit(EXIT_FAILURE);
	}

        fprintf(stderr, "Initializing database...");
        if (!db_init(config_get_db_path())) {
                fprintf(stderr, "Failed to initialize database\n");
                exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Loading rippers database...");
        load_rippers(opt_ripperspath);

        /* Get existing track count for statistics */
        allcount = db_get_track_count();
        fprintf(stderr, "\nExisting database has %d tracks.\n", allcount);
	
	/*
 	 * Setup a "report progress" alarm 
 	 */
	signal(SIGALRM, &report_scanning_progress);
	alarm(1);

        /* Index paths from command line */
        for (i = optind; i < argc; i++)
                start_recurse_disc(argv[i]);
        
        /* If no command-line paths, use configured index paths */
        if (!threadcount) {
                if (global_config.index_paths_count > 0) {
                        fprintf(stderr, "Indexing %d path(s) from config:\n", 
                                global_config.index_paths_count);
                        for (i = 0; i < global_config.index_paths_count; i++) {
                                fprintf(stderr, "  [%d] %s\n", i + 1, 
                                        global_config.index_paths[i]);
                                start_recurse_disc(global_config.index_paths[i]);
                        }
                } else {
                        fprintf(stderr, "Warning: No index paths configured. "
                                "Please edit %s/config.toml\n", xdg_config_dir);
                }
        }
        
        for (i = 0; i < threadcount; i++) {
                pthread_join(threads[i], NULL);
        }

	/*
 	 * No more alarms 
 	 */
	alarm(0);                                                 

        fprintf(stderr, "\nglaciera-indexer: total files: %d  new files: %d\n", total_files, new_files);

        db_close();
        exit(0);
}
