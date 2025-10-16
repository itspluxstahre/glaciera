// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>

/*
 * music.c - Music module framework
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
 * Also, you don't have to change _anything_ in the glaciera-indexer/glaciera programs.
 */

// System headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Local headers
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
	bool (*isit)(char *, int);
	bool (*info)(char *, struct tuneinfo *);
	bool (*metadata)(char *, struct track_metadata *);
	void (*play)(char *);
	struct filetype *next;
};

/* -------------------------------------------------------------------------- */

static struct filetype *fthead = NULL;

static void music_register_filetype(bool (*isitproc)(char *, int),
    bool (*infoproc)(char *, struct tuneinfo *), bool (*metaproc)(char *, struct track_metadata *),
    void (*playproc)(char *)) {
	struct filetype *ft;

	ft = malloc(sizeof(*ft));
	ft->isit = isitproc;
	ft->info = infoproc;
	ft->metadata = metaproc;
	ft->play = playproc;
	ft->next = fthead;
	fthead = ft;
}

/* -------------------------------------------------------------------------- */

struct filetype *music_isit(char *filename) {
	struct filetype *ft;
	int len = strlen(filename);

	for (ft = fthead; ft; ft = ft->next) {
		if (ft->isit(filename, len))
			return ft;
	}
	return NULL;
}

/* -------------------------------------------------------------------------- */

bool music_info(char *filename, struct tuneinfo *si) {
	struct filetype *ft;

	ft = music_isit(filename);
	return ft ? ft->info(filename, si) : false;
}

/* -------------------------------------------------------------------------- */

bool music_metadata(char *filename, struct track_metadata *meta) {
	struct filetype *ft;

	ft = music_isit(filename);
	if (!ft || !ft->metadata)
		return false;
	return ft->metadata(filename, meta);
}

/* -------------------------------------------------------------------------- */

void music_play(char *filename) {
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
#include "mod_flac.h"
#include "mod_mp3.h"
#include "mod_ogg.h"
#include "mod_pls.h"

/*
 * Called from glaciera-indexer & glaciera to build the list of supported music formats.
 */

void music_register_all_modules(void) {
	/*
	 * INSERT NEW music_register_filetype's HERE
	 * music_register_filetype(&XXX_isit, &XXX_info, &XXX_metadata, &XXX_play);
	 * =========================================
	 */
	music_register_filetype(&pls_isit, &pls_info, NULL, &pls_play);
	music_register_filetype(&flac_isit, &flac_info, &flac_metadata, &flac_play);
	music_register_filetype(&ogg_isit, &ogg_info, &ogg_metadata, &ogg_play);
	music_register_filetype(&mp3_isit, &mp3_info, &mp3_metadata, &mp3_play);
}
