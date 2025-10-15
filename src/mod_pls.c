/*
 * mod_pls - makes mp3berg know about .pls-files.
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
#include <unistd.h>

#include "common.h"

/* -------------------------------------------------------------------------- */

/*
 * Return true if the file contains lines with URL's.
 * Yes, the same as a "grep http filename" but much faster !
 */
bool pls_info(char *filename, struct tuneinfo *ti)
{
	(void)ti;
	FILE *f;
	char buf[1024];
	bool has_httplines = false;
	
	f = fopen(filename, "r");
	if (f) {
		while (fgets(buf, sizeof(buf), f)) {
			if (strstr(buf, "http"))
				has_httplines = true;
		}
		fclose(f);
	}
        return has_httplines;
}

bool pls_isit(char *s, int len)
{
        if ((len > 4) &&
            (s[len - 4] == '.') &&
            (s[len - 3] == 'P' || s[len - 3] == 'p') &&
            (s[len - 2] == 'L' || s[len - 2] == 'l') &&
            (s[len - 1] == 'S' || s[len - 1] == 's')) {
		return true;
	}
        
	if ((len > 4) &&
            (s[len - 4] == '.') &&
            (s[len - 3] == 'M' || s[len - 3] == 'm') &&
            (s[len - 2] == '3' || s[len - 2] == '3') &&
            (s[len - 1] == 'U' || s[len - 1] == 'u')) {
		return true;
	}

	return false;
}

void pls_play(char *filename)
{
	/*
 	 * Don't spam the /tmp/mp3berg.out too much, but don't be to quiet either... 
 	 * Unless the -quiet flag is present, the logfile is flooded with
 	 * several progress messages per second. 
 	 * The -quiet flag lets the StreamTitle messages get through. 
 	 */
	execlp("mplayer", "mplayer", "-quiet", "-noconsolecontrols", "-playlist", filename, NULL);	
}
