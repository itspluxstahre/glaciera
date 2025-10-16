#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef unsigned long BIGPTR;

/*
** 4+4+2+2+1+1
*/
struct tuneinfo {
	int filesize;
	time_t filedate;
	short duration; /* 0xffff seconds = 1092 hours = 45.5 days... */
	short bitrate;
	unsigned char genre;
	unsigned char rating;
};

struct tune {
	char *path;
	char *display;
	char *search;
	struct tuneinfo *ti;
};

struct smalltune {
	char *path;
	struct tuneinfo *ti;
};

struct tune0 {
	BIGPTR p1;
	BIGPTR p2;
	BIGPTR p3;
	BIGPTR p4;
};

struct track_metadata {
	char *title;
	char *artist;
	char *album;
	char *track;
	int track_number;
};

extern char tolowerarray[256];

bool inrange(int v, int min, int max);
bool is_typeable_key(int key);
void build_fastarrays(void);
void only_searchables(char *src);
void sanitize_user_input(char *src);
void chop(char *buf);
int trim(char s[]);
int fuzzy(char *haystack, char *needle);
void swap(struct tune **a, struct tune **b);
char *strrev(char *str);

typedef unsigned int BITS;
void bitset(BITS *abits, int i);
void bitclr(BITS *abits, int i);
bool bittest(BITS *abits, int i);
void bitnull(BITS *abits, int bits);
BITS *bitalloc(int bits);

void track_metadata_init(struct track_metadata *meta);
void track_metadata_clear(struct track_metadata *meta);
