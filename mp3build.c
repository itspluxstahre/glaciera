#define noDEBUG
/*
 * mp3build
 *      generates the database for mp3berg 
 *
 * Goal 1: Recursive scan of all mp3's
 *      Takes ~5 minutes on my old P120 40MB RedHat9 machine
 *      to scan ~71000 mp3's spread over ten hard disks.
 *
 * Goal 2: Calculate the song length for every mp3.
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
 *	System: P200 96MB FC3 machine 185000mp3s 8 physical disks
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/mount.h>

#include "common.h"
#include "svn_version.h"
#include "music.h"

void * g_mm[5];
int g_mmsize[5];
FILE * g_db[5];
struct tune0 g_posblock;

struct smalltune *smalltunes = NULL;
int allcount = 0;
int total_files = 0;
double total_bytes = 0;
int new_files = 0;
time_t timeprogress = 0;
int opt_generate_allmp3db = FALSE;
int opt_force_build = FALSE;
int opt_skip_file_info = FALSE;

pthread_mutex_t filemutex = PTHREAD_MUTEX_INITIALIZER;

/* --------------------------------------------------------------------------- */

void load_all_mp3_database(const char *srcdir)
{
        int i;
        int f[5];
        struct stat ss;
        char buf[1024];
        struct tune0 *alltunesbase;

        for (i = 0; i < 5; i++) {
                snprintf(buf, sizeof(buf), "%s%d.db", srcdir, i);
                f[i] = open(buf, O_RDONLY);
                if (-1 == f[i])
                        return;
                fstat(f[i], &ss);
                g_mmsize[i] = ss.st_size;
                if (i == 0) 
                        allcount = g_mmsize[i] / sizeof(struct tune);
		g_mm[i] = mmap(0, g_mmsize[i], PROT_READ, MAP_SHARED, f[i], 0);
                close(f[i]);
        }

        /*
         * Make the smalltunes pointers point to the data
         */
	smalltunes = malloc(sizeof(struct smalltune) * allcount);
        for (alltunesbase = g_mm[0], i = 0; i < allcount; i++, alltunesbase++) {
		smalltunes[i].path = (void*) ((BIGPTR) alltunesbase->p1 + (BIGPTR) g_mm[1]);
		smalltunes[i].ti   = (void*) ((BIGPTR) alltunesbase->p4 + (BIGPTR) g_mm[4]);
	}
}

/* --------------------------------------------------------------------------- */

int sort_function(const void *a, const void *b)
{
        return strcmp(((struct smalltune *) a)->path,
                      ((struct smalltune *) b)->path);
}

void sort_mp3_database(void)
{
        qsort((void *) smalltunes, allcount, sizeof(struct smalltune), sort_function);
}

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

#if 0
        for(i=32;i<100;i++) {
                printf("%3d %c   %3d --  %3d\n", i, i,qsearch[i].lo, qsearch[i].hi);
                for (ch = qsearch[i].lo; ch <= qsearch[i].hi; ch++)
                        if (ch)
                        printf("                 %3d *%s*\n", ch, rippers[ch].str);
        }
exit(0);
        for(i=0;i<ripperscount;i++)
                printf("%5d %3d %s\n", i, rippers[i].len, rippers[i].str);
#endif
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

void strip_path_ripper(char *s)
{
        int i, j;
        char *p;
        int slen = strlen(s);

        return;

        for (i = 0; i < ripperscount; i++) {
                for (j = slen - 1; j; j--) {
                        if ('/' == s[j]) {
                                p = &s[j] - rippers[i].len;
                                if (0 == strncasecmp(p, rippers[i].str, rippers[i].len)) {
                                        memmove(p, p + rippers[i].len, rippers[i].len+2);
                                        break;
                                }
                        }
                }
        }
}

/* --------------------------------------------------------------------------- */

void get_cached_info(char *filename, struct tuneinfo *ti)
{
        struct smalltune *t = NULL;
        struct smalltune key;

        if (allcount) {
                key.path = &filename[0];
                t = bsearch(&key, (void *) smalltunes, allcount, sizeof(struct smalltune),
                                  sort_function);
        }

        if (t)
		memcpy(ti, t->ti, sizeof(struct tuneinfo));
	else if (!opt_skip_file_info) {
		if (music_info(filename, ti))
			new_files++;
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
        char *p;
        int i;
        char fullpath[1024*4];
        char gbuf[1024*4];

        /*
         * Run ftell against db1, db2, db3 and db4
         */
        fwrite(&g_posblock, 1, sizeof(g_posblock), g_db[0]);

        /*
         * 1. Write "fullpath" to 1.db
         */
        g_posblock.p1 += fwrite(afullpath, 1, strlen(afullpath) + 1, g_db[1]);

        /*
         * 2. Write "display" to 2.db
         * Trim filename and keep all unique characters
         */
        strcpy(fullpath, dir);
        trim_display_path(fullpath);	
        strip_ripper(fullpath);
        strcat(fullpath, "/");
        p = fullpath + strlen(fullpath);
        for (i = 0; filename[i]; i++) {
		if (bittest(keepers, i))
                        *p++ = filename[i];
        }
        *p = 0;
	strcpy(gbuf, massage_full_path(gbuf, fullpath));
        trim_display_path(gbuf);
        strip_ripper(gbuf);
        trim_double_spaces(gbuf);
        trim_double_minuses(gbuf);
        trim_minus_space_minus(gbuf);
        trim_space_dot_space(gbuf);
#ifdef DEBUG
printf("*******");
printf(gbuf);	
printf("*******\n");
#endif

	/*
	 * !!2005-05-10 
	 * If the name begins with - or . or , step to
	 * where the characters begins
	 */
	p = gbuf;
	while (*p && !isalnum(*p))
		p++;
        g_posblock.p2 += fwrite(p, 1, strlen(p) + 1, g_db[2]);

        /*
         * 3. There is no step 3. "search" are calculated later
         */

        /*
         * 4. Write "tuneinfo" to 4.db
         */
        g_posblock.p4 += fwrite(pfti, 1, sizeof(struct tuneinfo), g_db[4]);
}

/*
 * Find duplicate titles for this directory
 * "01. Band - One"
 * "02. Band - Two"
 * "03. Band - Three"
 *
 * Here, the string "Band" is
 */
 
void find_redundant_song_names(DIR *pdir, BITS keepers[])
{
        int musicfiles;
        char basefilename[256];
        int samecolumn[256];
        int sumcolumn[256];
	int trackcolumn[256];
        struct dirent *sd;
        char *p;
        int i;
	int justnames;
	int trackstarts;
	int trackcount;

        musicfiles = 0;
        memset(basefilename, 0, sizeof(basefilename));
        memset(samecolumn, 0, sizeof(samecolumn));
        memset(trackcolumn, 0, sizeof(trackcolumn));
        memset(sumcolumn, 0, sizeof(sumcolumn));
	
        while (NULL != (sd = readdir(pdir))) {
                if (!music_isit(sd->d_name))
                        continue;

                musicfiles++;
		
		/*
		 * Chop the file extention
		 */
	        p = strrchr(sd->d_name, '.');
	       	if (p)
        	       	*p = 0;
                
		if (!basefilename[0])
                        strcpy(basefilename, sd->d_name);

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

	/*
	 * Keep all characters if there is only one file in the directory
	 */
        if (0 == musicfiles || 1 == musicfiles) {
		for (i = 0; i < 256; i++) 
			bitset(keepers, i);
		return;
	}

        /*
         * Find which characters to keep
         */
        for (i = 0; i < 256; i++) {
                if (samecolumn[i] != musicfiles)
                        samecolumn[i] = FALSE;
                if (trackcolumn[i] != musicfiles)
                        trackcolumn[i] = FALSE;
	}
	
#ifdef DEBUG	
	fprintf(stderr,"\n0123456789012345678901234567890\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%c", basefilename[i] ? basefilename[i] : ' ');
	fprintf(stderr,"\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%s", samecolumn[i] ? "=" : " ");
	fprintf(stderr,"\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%s", trackcolumn[i] ? "#" : " ");
	fprintf(stderr,"\n");
#endif

	/*
	 * Remove repeated "cd1" from "cd1-01-song"
	 *			      "cd1-02-song"
	 *			      "cd1-03-song"
	 *			      "cd1-04-song"
	 * Remove repeated "cd1" from "01-cd1-song"
	 *                            "02-cd1-song"
	 *                            "03-cd1-song"
	 *                            "04-cd1-song"
	 */	
        for (i = 0; i < 256; i++) {
		if (trackcolumn[i]) {
			sumcolumn[i] /= musicfiles;
			if (sumcolumn[i] == basefilename[i] && samecolumn[i]) {
#ifdef DEBUG				
				fprintf(stderr, "\n%d:ACC=%d FILES=%d", i,sumcolumn[i], musicfiles);
#endif			
				samecolumn[i] = TRUE;
				trackcolumn[i] = FALSE;
			}
		}
	}
	

	/*
	 * Clear xxxx from "xxxx03.song"
	 * Find from left the first non-repeated digit column
	 * Clean from start of name to the digits
 	 */        
	trackstarts = -1;
        for (i = 255; i; i--) {
		while (i>=0 && trackcolumn[i]) {
		 	trackstarts = i;
			i--;
		}
		if (trackstarts != -1)
			 break;
	}
        for (i = 0; i < trackstarts; i++) {
		trackcolumn[i] = FALSE;
		samecolumn[i] = TRUE;
	}

	/*
	 * Fix the cases where the leading 0 digit is removed
	 */	
	trackcount = 0;
	for (i = 0; i < 256; i++)
		trackcount += trackcolumn[i] ? 1 : 0;
	if (1 == trackcount) {
		for (i = 1; i < 256; i++)
			if (trackcolumn[i])
				trackcolumn[i-1] = TRUE;					
	}
	
#ifdef DEBUG	
	fprintf(stderr,"\nTRACKSTARTS=%d\n", trackstarts);
	fprintf(stderr,"\n0123456789012345678901234567890\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%c", basefilename[i] ? basefilename[i] : ' ');
	fprintf(stderr,"\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%s", samecolumn[i] ? "=" : " ");
	fprintf(stderr,"\n");
        for (i = 0; i < 40; i++) 
		fprintf(stderr,"%s", trackcolumn[i] ? "#" : " ");
	fprintf(stderr,"\n");
#endif	

	justnames = TRUE;
        for (i = 0; i < 256; i++) {
		if (trackcolumn[i])
			justnames = FALSE;		
	}
	if (justnames) {
	        for (i = 0; i < 256; i++) 
                        samecolumn[i] = FALSE;
	}

	for (i = 0; i < 256; i++) {
		/*
		 * Keep those characters that isn't repeated in the same column
		 */		
                if (!samecolumn[i])
			bitset(keepers, i);
			
		/*
		 * Ensure that the tracknumber column doesn't disappear
		 */		
		if (trackcolumn[i])
			bitset(keepers, i);		
	}
}

void * prim_recurse_disc(void *argdir)
{
        DIR *pdir;
        struct dirent64 *sd;
	struct dirent64 *sdbuf;
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
                    
	sdbuf = (struct dirent64 *) malloc(offsetof(struct dirent64, d_name) + NAME_MAX + 1);

        rewinddir(pdir);
//	while (NULL != (sd = readdir(pdir))) {

	while (readdir64_r(pdir, sdbuf, &sd) == 0 && sd) {
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
	free(sdbuf);
        return NULL;
}

/* --------------------------------------------------------------------------- */

void * mm2;

int tune0_sort(const void * mm0_a, const void * mm0_b)
{
        return strcasecmp((char *) ((struct tune0 *) mm0_a)->p2 + (BIGPTR)mm2,
                          (char *) ((struct tune0 *) mm0_b)->p2 + (BIGPTR)mm2);
}

void sort_0_file(void)
{
        int db0;
        int db2;
        int size0;
        int size2;
        struct stat ss;
        struct tune0 * mm0;

        db0 = open("/tmp/0.db", O_RDWR);
        fstat(db0, &ss);
        size0 = ss.st_size;
        mm0 = mmap(0, size0, PROT_READ|PROT_WRITE, MAP_SHARED, db0, 0);

        db2 = open("/tmp/2.db", O_RDONLY);
        fstat(db2, &ss);
        size2 = ss.st_size;
        mm2 = mmap(0, size2, PROT_READ, MAP_SHARED, db2, 0);
        qsort(mm0, size0 / sizeof(struct tune0), sizeof(struct tune0), tune0_sort);
        munmap(mm2, size2);
        close(db2);

        munmap(mm0, size0);
        close(db0);
}

/* --------------------------------------------------------------------------- */

void copy_db(const char *dstdir)
{
        int i;
        int f[5];
        BIGPTR mm[5];
        unsigned int mmsize[5];
        struct stat ss;
        char buf[1024];
        FILE * output[5];
        char * p;
        struct tune0 posblock;
        struct tune0 * base;
        time_t now;
        int prev = 0;
        FILE * allmp3db = NULL;
	char oldname[255];
	char newname[255];
	int error;

        /*
         * Setup memory maps from source files
         */
        for (i = 0; i < 5; i++) {
                snprintf(buf, sizeof(buf), "/tmp/%d.db", i);
                f[i] = open(buf, O_RDONLY);
                fstat(f[i], &ss);
                mmsize[i] = ss.st_size;
                if (0 == i)
                        allcount = mmsize[i] / sizeof(struct tune);
                mm[i] = (BIGPTR) mmap(0, mmsize[i], PROT_READ, MAP_SHARED, f[i], 0);
                close(f[i]);
        }

        /*
         * Create the destination files
         */
        for (i = 0; i < 5; i++) {
                snprintf(buf, sizeof(buf), "%s%d.db.tmp", dstdir, i);
                output[i] = fopen(buf, "w");
                if (!output[i]) {
			fprintf(stderr, "\ncopy_db: Cannot create our database!\n\nFatal error! Quiting!\n");
			exit(0);
		}
        }	
	if (opt_generate_allmp3db) {
                snprintf(buf, sizeof(buf), "%sallmp3.db.tmp", dstdir);
	        allmp3db = fopen(buf, "w");
	}

        memset(&posblock, 0, sizeof(posblock));
        for (base = (void*)mm[0], i = 0; i < allcount; i++, base++) {
                now = time(NULL);
                if (now > timeprogress) {
                        fprintf (stderr, "%7d (%7d/sec)[%-60s]\r",
                                i,
                                i-prev,
                                "############################################################" +
                                59 - (int) (60L * i / allcount));
                        timeprogress = now;
                        prev=i;
                }

                /*
                 * Write the new index file
                 */
                fwrite(&posblock, 1, sizeof(posblock), output[0]);

                p = (char *) (base->p1 + mm[1]);
                posblock.p1 += fwrite(p, 1, strlen(p)+1, output[1]);
                if (allmp3db)
			fprintf(allmp3db, "%s\r\n", p);

                p = (char *) (base->p2 + mm[2]);
                posblock.p2 += fwrite(p, 1, strlen(p)+1, output[2]);
                if (allmp3db)
                        fprintf(allmp3db, "%s\r\n", p);

                strcpy(buf, (char *) (base->p2 + mm[2]));
                only_searchables(buf);
                posblock.p3 += fwrite(buf, 1, strlen(buf)+1, output[3]);

                p = (char *) (base->p4 + mm[4]);
                posblock.p4 += fwrite(p, 1, sizeof(struct tuneinfo), output[4]);
        }

        /*
         * Close destination files
         */
        for (i = 0; i < 5; i++) {
                munmap((void *) mm[i], mmsize[i]);
                fclose(output[i]);
        }
        if (allmp3db)
                fclose(allmp3db);
		
	/*
	 * Now the copying is done.
	 * Atomically rename the .tmp-files to the real files.
	 */
        for (i = 0; i < 5; i++) {
                snprintf(oldname, sizeof(oldname), "%s%d.db.tmp", dstdir, i);
                snprintf(newname, sizeof(newname), "%s%d.db", dstdir, i);
		error = rename(oldname, newname);
		if (error) {
	                fprintf(stderr, "\nError renaming '%s' to '%s'", oldname, newname);
		}
	}
        if (allmp3db) {
                snprintf(oldname, sizeof(oldname), "%sallmp3.db.tmp", dstdir);
                snprintf(newname, sizeof(newname), "%sallmp3.db", dstdir);
		error = rename(oldname, newname);
		if (error) {
	                fprintf(stderr, "\nError renaming '%s' to '%s'", oldname, newname);
		}
	}
}

/* --------------------------------------------------------------------------- */

pthread_t threads[100];
int threadcount = 0;

void * prim_just_build_files(void * argdir)
{
        char * dir = argdir;
        int dirlen;
        int i;
        struct tune t;

        /*
         * Don't use the alltunes array to search for filenames.
         * The array points to different places in the 0.db, 1.db files,
         * trashing the VM-cache.
         * The fix is to "read" the (already sorted) 0.db file directly via 
         * the memory-map pointer. That makes all accesses linear.
         *
         * with alltunes array (BAD):
         *  6 4 3 9 28 89 2 5
         * without alltunes array (GOOD):
         *  2 3 4 5 6 9 28 89
         */
        dirlen = strlen(dir);
        t.path    = g_mm[1];
        t.display = g_mm[2];
        t.ti      = g_mm[4];

        for (i = 0; i < allcount; i++) {
                if (0 == strncmp(t.path, dir, dirlen) && '/' == t.path[dirlen]) {
                        pthread_mutex_lock(&filemutex);

                                         fwrite(&g_posblock, 1, sizeof(g_posblock),      g_db[0]);
                        g_posblock.p1 += fwrite(t.path,      1, strlen(t.path)    + 1,   g_db[1]);
                        g_posblock.p2 += fwrite(t.display,   1, strlen(t.display) + 1,   g_db[2]);
                        g_posblock.p4 += fwrite(t.ti,        1, sizeof(struct tuneinfo), g_db[4]);

                        total_files++;
			total_bytes += t.ti->filesize;

                        pthread_mutex_unlock(&filemutex);
                }

                t.path    += strlen(t.path) + 1;
                t.display += strlen(t.display) + 1;
                t.ti++;
        }

        return NULL;
}

int is_path_in_mounts(char *path)
{
	FILE *f;
	char buf[1024];
	int hasmount = FALSE;

	f = fopen("/proc/mounts", "r");
	if (f) {
		while (fgets(buf, sizeof(buf), f)) {
			if (strstr(buf, path))
				hasmount = TRUE;
		}
		fclose(f);
	}
	return hasmount;
}

int path_has_files(char *path)
{
	DIR *pdir;
	struct dirent *sd;
	int result = FALSE;

	pdir = opendir(path);
	if (pdir) {
		while (NULL != (sd = readdir(pdir))) {
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
        unsigned long int stored_free = -1;
	int err;
	int use_cache = FALSE;

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
	snprintf(freefilename, sizeof(freefilename), "%s%s.free", opt_mp3path, buf);

        f = fopen(freefilename, "r");
        if (f) {
                fscanf(f, "%lu", &stored_free);
                fclose(f);
        }

	if (opt_force_build)
		stored_free = -1;

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
		use_cache = stored_free != -1;

        if (use_cache) {
                fprintf(stderr, "\nJust parsing mp3-files '%s'...", dir);
		fflush(stderr);

                pthread_create(&threads[threadcount++],
                                NULL,
                                &prim_just_build_files,
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
                        fprintf(f, "%lu\n", df.f_bfree);
                        fclose(f);
                }
        }
}

/* --------------------------------------------------------------------------- */

int can_create_database(char * dir)
{
	char buf[100];
	FILE *f;
	int writeable = FALSE;

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
		writeable = TRUE;
	}
	
	return writeable;
}

/* --------------------------------------------------------------------------- */

void print_version(void)
{
        fprintf(stderr, "Database builder for MP3BERG - Version 4.0.%s - %s\n", 
			svn_version(), 
			__DATE__ " " __TIME__);
        fprintf(stderr, "Copyright (c) Krister Brus 2003-2010 <kristerbrus@fastmail.fm>\n");
}

/* --------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
        int i;
	int arg;
        time_t start;

        while ((arg = getopt(argc, argv, "hvwfs")) > -1) {
                switch (arg) {
		case 'w':
			opt_generate_allmp3db = TRUE;
			break;
		case 'f':
			opt_force_build = TRUE;
			break;
		case 's':
			opt_skip_file_info = TRUE;
			break;
                case 'h':
                case '?':
                        print_version();
                        printf("usage: mp3build [-h] [-w] [-f] [-s]\n");
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
	read_rc_file();
	sanitize_rc_parameters(FALSE);
	music_register_all_modules();
        build_fastarrays();
        memset(&g_posblock, 0, sizeof(g_posblock));

	if (!can_create_database(opt_mp3path)) {
		fprintf(stderr, "Error: The path '%s' must be writeable.\n", opt_mp3path);
		exit(1);
	}

        fprintf(stderr, "Loading rippers database...");
        load_rippers(opt_ripperspath);

        fprintf(stderr, "\nLoading mp3 database...");
        load_all_mp3_database(opt_mp3path);

        fprintf(stderr, "\nSorting mp3 database, (%d) songs...", allcount);
        start = time(NULL);
        sort_mp3_database();
        fprintf(stderr, " sort took %ld seconds.\n", time(NULL) - start);

        for (i = 0; i < 5; i++) {
                char buf[100];
                snprintf(buf, sizeof(buf), "/tmp/%d.db", i);
                g_db[i] = fopen(buf, "w");
        }
	
	/*
 	 * Setup a "report progress" alarm 
 	 */
	signal(SIGALRM, &report_scanning_progress);
	alarm(1);

        for (i = optind; i < argc; i++)
                start_recurse_disc(argv[i]);
        if (!threadcount)
                start_recurse_disc(opt_mp3path);
        for (i = 0; i < threadcount; i++) {
                pthread_join(threads[i], NULL);
        }

	/*
 	 * No more alarms 
 	 */
	alarm(0);                                                 

        for (i = 0; i < 5; i++) {
                if (g_mm[i])
                        munmap(g_mm[i], g_mmsize[i]);
                fclose(g_db[i]);
        }

        fprintf(stderr, "\nSorting index file...");
        start = time(NULL);
        sort_0_file();
        fprintf(stderr, " sort took %ld seconds.", time(NULL) - start);

        fprintf(stderr, "\nCopying database files...");
        copy_db(opt_mp3path);

        fprintf(stderr, "\nmp3build: total files: %d  new files: %d\n", total_files, new_files);

        exit(0);
}
