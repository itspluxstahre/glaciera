/*
 * mod_mp3 - makes mp3berg know about .mp3-files.
 *
 * Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>
 * Scanning code based on C# code from:
 * http://www.developeru.info/PermaLink,guid,6ac0aff9-0223-4a05-94ac-995feaac683a.aspx
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

#include "common.h"

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

static inline int mp3_getFrameSync(unsigned long h)
{
        return (int) ((h >> 21) & 0x7FF);
}

static inline int mp3_getVersionIndex(unsigned long h)
{
        return (int) ((h >> 19) & 0x03);
}

static inline int mp3_getLayerIndex(unsigned long h)
{
        return (int) ((h >> 17) & 0x03);
}

static inline int mp3_getBitrateIndex(unsigned long h)
{
        return (int) ((h >> 12) & 0x0F);
}

static inline int mp3_getFrequencyIndex(unsigned long h)
{
        return (int) ((h >> 10) & 0x03);
}

static inline int mp3_getEmphasisIndex(unsigned long h)
{
        return (int) (h & 0x03);
}

static inline int mp3_getModeIndex(unsigned long h)
{
        return (int) ((h >> 6) & 0x03);
}

static inline int mp3_is_valid_header(unsigned long h)
{
        return (((mp3_getFrameSync(h)     ) == 0x7FF) &&
                ((mp3_getVersionIndex(h)  ) != 1    ) &&
                ((mp3_getLayerIndex(h)    ) != 0    ) &&
                ((mp3_getBitrateIndex(h)  ) != 0    ) &&
                ((mp3_getBitrateIndex(h)  ) != 15   ) &&
                ((mp3_getFrequencyIndex(h)) != 3    ) &&
                ((mp3_getEmphasisIndex(h) ) != 2    ));
}

static inline int mp3_get_frequency(unsigned long h)
{
        static int tableFreq[4][3] = {
                {32000, 16000,  8000},  /* MPEG 2.5 */
                {    0,     0,     0},  /* reserved */
                {22050, 24000, 16000},  /* MPEG 2   */
                {44100, 48000, 32000}   /* MPEG 1   */
        };

        return tableFreq[mp3_getVersionIndex(h)][mp3_getFrequencyIndex(h)];
}

static int mp3_calc_bit_rate(unsigned long h, size_t filesize, int variable_frames)
{
        static int tableBitRate[2][3][16] = {
        {       /* MPEG 2 & 2.5 */
        {0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0},    /* Layer III */
        {0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0},    /* Layer II */
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}     /* Layer I */
        },
        {       /* MPEG 1 */
        {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0}, /* Layer III */
        {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0}, /* Layer II */
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}  /* Layer I */
        }
        };

        int result = 0;

        /*
         * If the file has a variable bitrate, then we return
         * an integer average bitrate, otherwise, we use a lookup
         * table to return the bitrate
         */
        if (variable_frames) {
                double medFrameSize = (double) filesize / (double) variable_frames;
                result = (int)
                  ((medFrameSize * (double) mp3_get_frequency(h)) /
                   (1000.0 * ((mp3_getLayerIndex(h) == 3) ? 12.0 : 144.0)));
        } else {
                result = tableBitRate[mp3_getVersionIndex(h) & 1]
                                     [mp3_getLayerIndex(h) - 1]
                                     [mp3_getBitrateIndex(h)];
        }

        return result;
}

static inline long mp3_calc_length_in_seconds(size_t filesize, int bitrate)
{
        int intKiloBitFileSize = (int) ((8 * filesize) / 1000);
        return bitrate ? ((long) (intKiloBitFileSize / bitrate)) : 0;
}

struct mp3_id3v1 {
        char id[3];         /* should be TAG */
        char title[30];
        char artist[30];
        char album[30];
        char year[4];
        char comment[30];
        unsigned char genre;
};

/* --------------------------------------------------------------------------- */

int mp3_info(char *filename, struct tuneinfo *ti)
{
        int f;
        unsigned long h;
        int variable_frames;
        unsigned char *pmm_start;
	unsigned char *pmm_end;
        int is_valid_mp3 = FALSE;
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
                return FALSE;
        }
        error = fstat(f, &ss);
        if (error) {
                close(f);
                fprintf(stderr, "\nmp3_read_info: fail in stat '%s'\n", filename);
                return FALSE;
        }
	
	/*
	 * From the excellent http://insights.oetiker.ch/linux/fadvise.html .
	 * Tell the OS NOT to cache the scanned mp3's into memory, 
	 * since they are only read once. 
	 */
#ifndef __MACOSX__
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
			
                h = (header[0] << 24) |
                    (header[1] << 16) |
                    (header[2] << 8)  |
                    (header[3]);

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
                if (header[0] == 'X' &&
                    header[1] == 'i' &&
                    header[2] == 'n' &&
                    header[3] == 'g') {
                        if (header[7] & 0x01) {
                                variable_frames = (header[ 8] << 24) |
                                                  (header[ 9] << 16) |
                                                  (header[10] << 8)  |
                                                  (header[11]);
                        }
                }

                ti->bitrate = mp3_calc_bit_rate(h, ti->filesize, variable_frames);
                ti->duration = mp3_calc_length_in_seconds(ti->filesize, ti->bitrate);

		/*
		 * Get genre from the ID3 tag
		 */
		pv1 = (void*) (pmm_end - 128);
		if (pv1->id[0] == 'T' &&
		    pv1->id[1] == 'A' &&
		    pv1->id[2] == 'G')
			ti->genre = pv1->genre;
		else
			ti->genre = 0xff;

#if 0
printf("%8d %4d %4d %4d %s\n", variable_frames, ti->duration, ti->bitrate, ti->genre, filename);
#endif

                is_valid_mp3 = TRUE;
                break;
        }

        munmap(pmm_start, ti->filesize);
        if (!is_valid_mp3 && ti->filesize)
                fprintf(stderr,"\nmp3_read_info: cannot find mp3-info for '%s'\n", filename);
        return is_valid_mp3;
}

/* -------------------------------------------------------------------------- */

int mp3_isit(char *s, int len)
{
        if ((len > 4) &&
            (s[len - 4] == '.') &&	    
            (s[len - 3] == 'M' || s[len - 3] == 'm') &&
            (s[len - 2] == 'P' || s[len - 2] == 'p') &&
            (s[len - 1] == '3')) {
		return TRUE;
	}	

	return FALSE;
}	

void mp3_play(char *filename)
{
	execlp(opt_mp3playerpath, opt_mp3playerpath, filename, NULL);	

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
