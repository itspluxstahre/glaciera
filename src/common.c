/*
 * common.c - Common stuff for mp3build and mp3berg
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

char opt_mp3path       [100] = "/Users/ellie/Music/mp3berg";
char opt_ripperspath   [100] = "/Users/ellie/Music/mp3berg/rippers";
char opt_mp3playerpath [100] = "mpg123";	/* Don't need absolute path thanks to execlp */
char opt_mp3playerflags[100] = "";
char opt_oggplayerpath [100] = "ogg123";	/* Don't need absolute path thanks to execlp */
char opt_oggplayerflags[100] = "";
char opt_mp3bergrcpath [100] = "/etc/mp3bergrc";
char opt_mp3bergrchomepath [100] = ".mp3bergrc";
char opt_allowprivatercfile[100] = "true";
char tolowerarray[256];

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

/*
 * Very clever and generic swap() routine. C++ complains, though...
 */
/*
#define Swap(a,b) ((int)a^=(int)b^=(int)a^=(int)b)
*/

void swap(struct tune **a, struct tune **b)
{
        struct tune *t;

        t = *a;
        *a = *b;
        *b = t;
}

bool is_typeable_key(int key)
{
        return ((key >= 'a') && (key <= 'z')) ||
               ((key >= 'A') && (key <= 'Z')) ||
               ((key >= '0') && (key <= '9'));
/*return isalnum(key);*/
}

static bool istypeablearray[256];
static char toupperarray[256];

void build_fastarrays(void)
{
        int i;

        for (i = 0; i < 256; i++) {
                istypeablearray[i] = is_typeable_key(i);
                toupperarray[i] = toupper(i);
                tolowerarray[i] = tolower(i);
        }
}

void only_searchables(char *src)
{
        char *dst;

        for (dst = src; *src; src++) {
                if (istypeablearray[*src & 0xff])
                        *dst++ = toupperarray[*src & 0xff];
        }
        *dst = '\0';
}

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

void chop(char *buf)
{
        char *p;
        p = strrchr(buf, '\n');
        if (p)
                *p = '\0';
}

/* trim: remove trailing blanks, tabs, newlines */
int trim(char s[])
{
	int n;
	
	for (n = strlen(s)-1; n >= 0; n--)
		if (s[n] != ' ' && s[n] != '\t' && s[n] != '\n')
			break;
	s[n+1] = '\0';
	return n;
}

/* -------------------------------------------------------------------------- */

char *strrev(char *str)
{
      char *pLeft, *pRight;

      if (!str || !*str)
            return str;
      for (pLeft = str, pRight = str + strlen(str) - 1; pRight > pLeft; ++pLeft, --pRight) {
            *pLeft  ^= *pRight;
            *pRight ^= *pLeft;
            *pLeft  ^= *pRight;
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

inline void bitset (BITS *abits, int i)     {        abits[i / BITSPERWORD] |=  (1<<(i % BITSPERWORD)); }
inline void bitclr (BITS *abits, int i)     {        abits[i / BITSPERWORD] &= ~(1<<(i % BITSPERWORD)); }
inline bool bittest(BITS *abits, int i)     { return (abits[i / BITSPERWORD] &   (1<<(i % BITSPERWORD))) != 0; }
inline void bitnull(BITS *abits, int bits)  {        memset(abits, 0, CALCBYTES(bits));                 }
inline BITS *bitalloc(int bits)             { return malloc(CALCBYTES(bits));                           }

/* -------------------------------------------------------------------------- */

#ifdef USE_STACK
int int_stack[10];
int stack_pos = 0;
void int_push(int v)
{
        int_stack[stack_pos++] = v;
}

void int_pop(int *v)
{
        *v = int_stack[--stack_pos];
}
#endif

/* -------------------------------------------------------------------------- */

char * find_actual_file_name(char *buf, char *fullfilename)
{
	glob_t g;
	char tmp[1000];
	char *p;
	char *dst;

	/*
	 * Translate /dir/subdir/RÄKSMÖRGÅS
	 *        to /dir/subdir/R*KSM*RG*S
	 */
	dst = tmp;
	for (p = fullfilename; *p; p++)
		*dst++ = isalnum(*p) || '/' == *p ? *p : '*';
	*dst = '\0';

	glob(tmp, GLOB_NOSORT, NULL, &g);
#ifdef DEBUG
	printf("XX%dXX%sXX", g.gl_pathc, g.gl_pathv[0]);
#endif
	if (1 == g.gl_pathc)
		strcpy(buf, g.gl_pathv[0]);
	else
		strcpy(buf, fullfilename);
	globfree(&g);
 
	return buf;
}

/* -------------------------------------------------------------------------- */
       	
/* Read a rcfile */

void parse_rc_file(char *rcfile) 
{
	char * p = NULL;
	FILE * f;
	char buf[100];
	
	f = fopen(rcfile, "r");
	if (!f)
		return;

	printf("\nReading rcfile: %s\n", rcfile);
        while (fgets(buf, sizeof(buf), f)) {
		trim(buf);			
		if      (0 == strcmp(buf, ":mp3path:"       	)) p = opt_mp3path;
		else if (0 == strcmp(buf, ":ripperspath:"   	)) p = opt_ripperspath;	
		else if (0 == strcmp(buf, ":mp3playerpath:" 	)) p = opt_mp3playerpath;
		else if (0 == strcmp(buf, ":mp3playerflags:"	)) p = opt_mp3playerflags;
		else if (0 == strcmp(buf, ":oggplayerpath:"	)) p = opt_oggplayerpath;
		else if (0 == strcmp(buf, ":oggplayerflags:"	)) p = opt_oggplayerflags;
		else if (0 == strcmp(buf, ":allowprivatercfile:")) p = opt_allowprivatercfile;
		else if (p) {
			strcpy(p, buf);
			p = NULL;
		}
	}
	fclose(f);
}

void read_rc_file(void)
{
	parse_rc_file(opt_mp3bergrcpath);
	
	if (0 == strcmp(opt_allowprivatercfile, "true" )) {
		char buf[100];
		strcpy(buf, gethomedir());
		strcat(buf, "/");
		strcat(buf, opt_mp3bergrchomepath);
		parse_rc_file(buf);
	}
}

void sanitize_rc_parameters(bool check_binpaths)
{
	bool error = false;
		
	if (opt_mp3path[strlen(opt_mp3path) - 1] != '/')
		strcat(opt_mp3path, "/");
	
	/* Yes not having the mp3/oggplayer IS critical.
	 * we WANT to exit when they are not found to
	 * make sure the user knows about it 
	 * However we only want to check that if we run
	 * the main mp3berg, the server where mp3build lives
	 * won't need thoose.
	 * // Plux Sat 08 Jul 2006 06:15:12 CEST 
	 */
/*	if (check_binpaths) {
	 
		if (access(opt_mp3playerpath, X_OK) != 0) 
	 	{
			printf("CRITICAL ERROR: %s was NOT found!\n", opt_mp3playerpath);
			error = true;
	 	}

		if (access(opt_oggplayerpath, X_OK) != 0)
		{
			printf("CRITICAL ERROR: %s was NOT found!\n", opt_oggplayerpath);
			error = true;
		}

		
		if (error)
			exit(EXIT_FAILURE);

	}*/	
}

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
	
