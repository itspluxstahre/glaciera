/*
 * mod_flac - makes mp3berg know about .flac-files.
 *
 * Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
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
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <FLAC/all.h>

#include "common.h"

/* -------------------------------------------------------------------------- */

bool flac_info(char *filename, struct tuneinfo *ti)
{
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
	ti->bitrate = 128;      /* TODO: Get values from file */
	ti->genre = 0xff;       /* TODO: Get values from file */

	return true;
}

bool flac_isit(char *s, int len)
{
        if ((len > 5) &&
            (s[len - 5] == '.') &&
            (s[len - 4] == 'F' || s[len - 4] == 'f') &&
            (s[len - 3] == 'L' || s[len - 3] == 'l') &&
            (s[len - 2] == 'A' || s[len - 2] == 'a') &&
            (s[len - 1] == 'C' || s[len - 1] == 'c')) {
		return true;
	}

	return false;
}

void flac_play(char *filename)
{
	/*
 	 * Uhm... discovered that ogg123 DOES infact play .flac-files, 
 	 * even better than flac123.
 	 *
 	 * flac123 has some strange bugs...
 	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=339450 
 	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=339454
 	 */
	execlp(opt_oggplayerpath, opt_oggplayerpath, filename, NULL);	
}

