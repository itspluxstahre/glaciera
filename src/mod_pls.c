// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>

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
 	 * Don't spam the /tmp/glaciera.out too much, but don't be to quiet either... 
 	 * Unless the -quiet flag is present, the logfile is flooded with
 	 * several progress messages per second. 
 	 * The -quiet flag lets the StreamTitle messages get through. 
 	 */
	execlp("mplayer", "mplayer", "-quiet", "-noconsolecontrols", "-playlist", filename, NULL);	
}
