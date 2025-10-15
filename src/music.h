#ifndef __MUSIC_H__
#define __MUSIC_H__

#include "common.h"

/*
 * This header file exports just the "struct filetype", 
 * not the struct members. 
 * The members are declared in music.c.
 */
struct filetype;

struct filetype * music_isit(char *filename);
bool music_info(char *filename, struct tuneinfo *si);
bool music_metadata(char *filename, struct track_metadata *meta);
void music_play(char *filename);
void music_register_all_modules(void);

#endif
