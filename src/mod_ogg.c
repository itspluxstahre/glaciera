/*
 * mod_ogg - makes mp3berg know about .ogg-files.
 *
 * Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
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

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "common.h"

/* -------------------------------------------------------------------------- */

int ogg_info(char *filename, struct tuneinfo *ti)
{
        FILE *f;
        OggVorbis_File vf;
        int error;
        struct stat ss;

        f = fopen(filename, "r");
        if (!f) {
                fprintf(stderr, "\nogg_read_info: fail in open '%s'\n", filename);
                return 0;
        }

        memset(&vf, 0, sizeof(OggVorbis_File));
        error = ov_open(f, &vf, NULL, 0);
        if (error < 0) {
                fclose(f);
                fprintf(stderr,"\nogg_read_info: Unable to understand '%s', errorcode=%d\n", filename, error);
                return 0;
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
        return 1;
}

int ogg_isit(char *s, int len)
{
        if ((len > 4) &&
            (s[len - 4] == '.') &&
            (s[len - 3] == 'O' || s[len - 3] == 'o') &&
            (s[len - 2] == 'G' || s[len - 2] == 'g') &&
            (s[len - 1] == 'G' || s[len - 1] == 'g')) {
		return TRUE;
	}

	return FALSE;
}

void ogg_play(char *filename)
{
	execlp(opt_oggplayerpath, opt_oggplayerpath, filename, NULL);	
}

