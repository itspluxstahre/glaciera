/*
 * music.c - This module contains the music module framework
 *
 * Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
 *
 * Three functions must be written for each supported music format:
 * 
 * 1. int XXX_isit(char *filename)
 *    returns true if the file has the extension .XXX
 *    
 * 2. int XXX_info(char *filename, struct tuneinfo *si)
 *    returns true if information about the file is calculated
 *    and put into the tuneinfo structure.
 *    
 * 3. void XXX_play(char *filename)
 *    plays the file with an external program
 *
 * The functions named music_* in this file are never meant 
 * to be modified when support for a new music format is created. 
 * It's just the music_register_all_modules function that needs
 * ONE additional call to setup the pointers to the new functions.
 *
 * Also, you don't have to change _anything_ in the mp3build/mp3berg programs.
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
#include "common.h"
#include "music.h"

/*
 * The header file exports just the "struct filetype", 
 * not the struct members. The members are declared here.
 * 
 * Refer to IBM's excellent article for background.
 * http://www-128.ibm.com/developerworks/power/library/pa-ctypes1/?ca=dgr-lnxw02CTypesP1
 */
struct filetype {
	int (*isit) (char *, int); 
	int (*info) (char *, struct tuneinfo *);
	void (*play) (char *); 
	struct filetype *next;
};

/* -------------------------------------------------------------------------- */

static struct filetype *fthead = NULL;

static void music_register_filetype(int (*isitproc) (char *, int), 
				    int (*infoproc) (char *, struct tuneinfo *),
				    void (*playproc) (char *)) 
{
	struct filetype *ft;
	
	ft = malloc(sizeof(*ft));
	ft->isit = isitproc;
	ft->info = infoproc;
	ft->play = playproc;
	ft->next = fthead;
	fthead = ft;
}

/* -------------------------------------------------------------------------- */

struct filetype * music_isit(char *filename)
{
	struct filetype *ft;
	int len = strlen(filename);
	
	for (ft = fthead; ft; ft = ft->next) {
		if (ft->isit(filename, len))
			return ft;
	}
	return NULL;
}

/* -------------------------------------------------------------------------- */

int music_info(char *filename, struct tuneinfo *si)
{
	struct filetype *ft;
	
	ft = music_isit(filename);
	return ft ? ft->info(filename, si) : 0;
}

/* -------------------------------------------------------------------------- */

void music_play(char *filename)
{
	struct filetype *ft;
	
	ft = music_isit(filename);
	if (ft)
		ft->play(filename);
}

/* -------------------------------------------------------------------------- */

/*
 * INSERT mod_XXX.h files here
 * ===========================
 */
#include "mod_mp3.h"
#include "mod_ogg.h"
#include "mod_flac.h"
#include "mod_pls.h"

/*
 * Called from mp3build & mp3berg to build the list of supported music formats.
 */

void music_register_all_modules(void)
{
	/* 
	 * INSERT NEW music_register_filetype's HERE
	 * music_register_filetype(&XXX_isit, &XXX_info, &XXX_play);
	 * =========================================
	 */
	music_register_filetype( &pls_isit,  &pls_info,  &pls_play);	
	music_register_filetype(&flac_isit, &flac_info, &flac_play);	
	music_register_filetype( &ogg_isit,  &ogg_info,  &ogg_play);	
	music_register_filetype( &mp3_isit,  &mp3_info,  &mp3_play);
}
