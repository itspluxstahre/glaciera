// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2007-2010 Krister Brus <kristerbrus@fastmail.fm>

// System headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Local headers
#include "common.h"

/* -------------------------------------------------------------------------- */

/*
 * Return true if the file contains lines with URL's.
 * Yes, the same as a "grep http filename" but much faster !
 */
bool pls_info(char *filename, struct tuneinfo *ti) {
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

bool pls_isit(char *s, int len) {
	if (len < 5)
		return false;

	const char *ext = s + len - 4;
	return (strcasecmp(ext, ".pls") == 0 || strcasecmp(ext, ".m3u") == 0);
}

void pls_play(char *filename) {
	/*
	 * Don't spam the /tmp/glaciera.out too much, but don't be to quiet either...
	 * Unless the -quiet flag is present, the logfile is flooded with
	 * several progress messages per second.
	 * The -quiet flag lets the StreamTitle messages get through.
	 */
	execlp("mplayer", "mplayer", "-quiet", "-noconsolecontrols", "-playlist", filename, NULL);
}
