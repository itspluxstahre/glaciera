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
#include <sys/statvfs.h>

#include "common.h"

/* --------------------------------------------------------------------------- */

bool TEMPLATE_info(char *filename, struct tuneinfo *ti)
{
        return false;
}

/* -------------------------------------------------------------------------- */

bool TEMPLATE_isit(char *s, int len)
{
	return false;
}	

/* -------------------------------------------------------------------------- */

void TEMPLATE_play(char *filename)
{
	execl("path-to-some-player", "path-to-some-player", filename, NULL);	
} 

