#ifndef __COMMON_H__
#define __COMMON_H__

#define FALSE 0
#define TRUE 1

#include <time.h>

typedef unsigned long  BIGPTR;

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

extern char opt_mp3path[100];
extern char opt_ripperspath[100];
extern char opt_mp3playerpath[100];
extern char opt_mp3playerflags[100];
extern char opt_oggplayerpath[100];
extern char opt_oggplayerflags[100];
extern char tolowerarray[256];

int inrange(int v, int min, int max);
int is_typeable_key(int key);
void build_fastarrays(void);
void only_searchables(char *src);
void sanitize_user_input(char *src);
void chop(char *buf);
int trim(char s[]);
int fuzzy(char *haystack, char *needle);
void swap(struct tune **a, struct tune **b);
char *strrev(char *str);

typedef unsigned int BITS;
void bitset (BITS *abits, int i);
void bitclr (BITS *abits, int i);
int  bittest(BITS *abits, int i);
void bitnull(BITS *abits, int bits);
BITS *bitalloc(int bits);

char *find_actual_file_name(char *buf, char *fullfilename);
char *gethomedir(void);
void read_rc_file(void);
void sanitize_rc_parameters(int checkbin_paths);

#endif
