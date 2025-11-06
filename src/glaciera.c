// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2000-2010 Krister Brus <kristerbrus@fastmail.fm>
// Portions Copyright (c) 1997 Kristian Wiklund <kw@dtek.chalmers.se>
//
// If you use this program, Kristian would appreciate a postcard sent to:
// Kristian Wiklund, Dept. of Computer Engineering,
// Chalmers University of Technology, S-412 96 GOTHENBURG, SWEDEN

// System headers
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdckdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(HAVE_LIBINTL_H)
#include <libintl.h>
#endif

// Local headers
#include "common.h"
#include "config.h"
#include "db.h"
#include "git_version.h"
#include "music.h"
#include "theme_preview.h"

#ifdef USE_GETTEXT
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

int opt_read_ahead = 1;

#define COLUMN_DELTA 20
#define EMPTY_SEARCH NULL
#define GLACIERA_PIPE "/tmp/glaciera.stdout"

enum {
	ARG_NORMAL,
	ARG_LENGTH,
	ARG_SIZE,
	ARG_DATE,
	ARG_BITRATE,
	ARG_GENRE,
	ARG_RATING,
	ARG_PATH,
#ifdef USE_FINISH
	ARG_FINISH,
#endif
	ARG_MAXVAL
};

struct {
	int lo;
	int hi;
} qsearch[256];

static int clamp_int(int value, int min_value, int max_value) {
	if (max_value < min_value)
		return min_value;
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

#ifdef USE_FINISH
static time_t time_max_value(void) {
	if ((time_t)-1 > 0)
		return (time_t)-1;
	int bits = (int)(sizeof(time_t) * CHAR_BIT);
	if (bits >= (int)(sizeof(uintmax_t) * CHAR_BIT))
		return (time_t)UINTMAX_MAX;
	uintmax_t mask = (UINTMAX_C(1) << (bits - 1)) - 1U;
	return (time_t)mask;
}

static time_t time_min_value(void) {
	if ((time_t)-1 > 0)
		return 0;
	return (time_t)(-time_max_value() - 1);
}

static bool time_add_safe(time_t a, time_t b, time_t *out) {
	intmax_t ai = (intmax_t)a;
	intmax_t bi = (intmax_t)b;
	intmax_t sum = ai + bi;
	intmax_t max = (intmax_t)time_max_value();
	intmax_t min = (intmax_t)time_min_value();
	if (sum > max || sum < min)
		return false;
	*out = (time_t)sum;
	return true;
}
#endif

struct tune *alltunes = NULL;
int allcount = 0;

struct tune **displaytunes = NULL;
int displaycount = 0;
#ifdef USE_FINISH
time_t *displaytimes = NULL;
time_t display_relative_end_time = 0;
#endif

struct tune **playlist = NULL;
int playlistcount = 0;

struct tune *now_playing_tune = NULL;
time_t started_playing_time = 0;
struct tune *tune_to_save_to_history = NULL;

WINDOW *win_top = NULL;
WINDOW *win_info = NULL;
WINDOW *win_middle = NULL;
WINDOW *win_bottom = NULL;
int tunenr = 0; /* the currently selected tune */
int toptunenr = 0; /* the tune at the top of middle window */
int middlesize = 0; /* size of displayed tunelist */

char playlist_dir[100] = "";
char search_string[100] = "";
char last_search_string[100] = "";
char latest_playlist_name[100] = "";

bool in_input_mode = false;
bool in_action = false;
int sort_arg = ARG_NORMAL;
time_t showprogressagain = 0;
int col_step = 0;
int key_count = 0;
bool wanna_quit = false;
bool needs_strcasecmp_sort = false;
bool paused = false;

pid_t player_pid = 0;
pthread_attr_t detachedattr;

void action(int key);
void find_and_play_next_handler(int dummyparam);
void do_state(int cmd);
void do_search(void);
static int read_input_key(void);

/* -------------------------------------------------------------------------- */

int same_key_twice_in_a_row(int *last_key_count) {
	int result;

	result = (1 + *last_key_count) == key_count;
	*last_key_count = key_count;

	return result;
}

/* -------------------------------------------------------------------------- */

/*
 * The alltunes array is already sorted (by mp3build) on the "display" field.
 * We can use that fact to do speedy searches/sorts by comparing just
 * _the pointers_ themselves. We don't need to strcmp() the strings !
 */
int displaytunes_sort(const void *a, const void *b) {
	return ((BIGPTR)(((struct tune *)a)->display)) - ((BIGPTR)(((struct tune *)b)->display));
}

/*
 * With SQLite backend, we use strdup() for strings, so we can't rely on pointer comparison.
 * We need to do a linear search by string content.
 */
struct tune *find_in_alltunes_by_display_pointer(char *display) {
	int i;

	/* Linear search through alltunes */
	for (i = 0; i < allcount; i++) {
		if (strcmp(alltunes[i].display, display) == 0) {
			return &alltunes[i];
		}
	}

	return NULL;
}

/*
 * Given a pointer to a tune in the alltunes array,
 * we can do an O(1) calculation to get that tunes index (0..allcount-1).
 */
int find_tune_number_by_alltunes_address(struct tune *t) {
	return ((BIGPTR)t - (BIGPTR)&alltunes[0]) / sizeof(struct tune);
}

void clear_displaytunes_prim(bool dosave) {
	int i;
	struct tune *tune;

	/* TODO: push the current displaylist to stack */
#ifdef USE_BACK
	if (dosave)
		do_state('S');
#else
	(void)dosave;
#endif

	/*
	 * Free all stale malloc's, allocated by show_available_playlists()
	 */
	for (i = 0; i < displaycount; i++) {
		tune = displaytunes[i];
		if (EMPTY_SEARCH == tune->search) {
			if (tune->path == tune->display) {
				free(tune->path);
				free(tune);
			}
		}
	}

	displaycount = 0;
	tunenr = 0;
	toptunenr = 0;
	col_step = 0;
	needs_strcasecmp_sort = false;
#ifdef USE_FINISH
	display_relative_end_time = 0;
#endif
}

void clear_displaytunes(void) {
	clear_displaytunes_prim(true);
}

static bool addtunetodisplay(struct tune *tune) {
	if (!tune)
		return false;

	size_t new_count = (size_t)displaycount + 1;
	if (new_count > SIZE_MAX / sizeof(*displaytunes))
		return false;

	struct tune **old_display = displaytunes;
	size_t display_bytes = 0;
	if (ckd_mul(&display_bytes, new_count, sizeof(*displaytunes)) != 0)
		return false;
	struct tune **new_display = malloc(display_bytes);
	if (!new_display)
		return false;
	if (old_display && displaycount > 0) {
		size_t copy_bytes = 0;
		if (ckd_mul(&copy_bytes, (size_t)displaycount, sizeof(*new_display)) != 0) {
			free(new_display);
			return false;
		}
		memcpy(new_display, old_display, copy_bytes);
	}

#ifdef USE_FINISH
	size_t time_count = new_count;
	size_t time_bytes = 0;
	if (ckd_mul(&time_bytes, time_count, sizeof(*displaytimes)) != 0) {
		free(new_display);
		return false;
	}
	time_t *old_displaytimes = displaytimes;
	time_t *new_displaytimes = malloc(time_bytes);
	if (!new_displaytimes) {
		free(new_display);
		return false;
	}
	if (old_displaytimes && displaycount > 0) {
		size_t time_copy_bytes = 0;
		if (ckd_mul(&time_copy_bytes, (size_t)displaycount, sizeof(*new_displaytimes))
		    != 0) {
			free(new_displaytimes);
			free(new_display);
			return false;
		}
		memcpy(new_displaytimes, old_displaytimes, time_copy_bytes);
	}
	displaytimes = new_displaytimes;
#endif

	displaytunes = new_display;

	displaytunes[displaycount] = tune;
#ifdef USE_FINISH
	displaytimes[displaycount] = display_relative_end_time;
	if (tune->ti) {
		int duration = tune->ti->duration;
		time_t duration_time = (time_t)duration;
		time_t updated = display_relative_end_time;
		if (time_add_safe(display_relative_end_time, duration_time, &updated)) {
			display_relative_end_time = updated;
		} else {
			display_relative_end_time
			    = (duration > 0) ? time_max_value() : time_min_value();
		}
	}
#endif
	displaycount++;
	if (EMPTY_SEARCH == tune->search)
		needs_strcasecmp_sort = true;

#ifdef USE_FINISH
	if (old_displaytimes)
		free(old_displaytimes);
#endif
	if (old_display)
		free(old_display);
	return true;
}

void addtexttodisplay(char *text, int duration, int filesize, time_t filedate) {
	struct tune *tune = malloc(sizeof(*tune));
	if (!tune)
		return;

	char *path_copy = strdup(text);
	if (!path_copy) {
		free(tune);
		return;
	}
	tune->display = path_copy;
	tune->path = path_copy;
	tune->search = EMPTY_SEARCH;
	tune->ti = malloc(sizeof(*tune->ti));
	if (!tune->ti) {
		free(path_copy);
		free(tune);
		return;
	}

	tune->ti->filesize = filesize;
	tune->ti->filedate = filedate;
	tune->ti->duration = duration;
	tune->ti->bitrate = 0;
	tune->ti->genre = 0;
	tune->ti->rating = 0;

	if (!addtunetodisplay(tune)) {
		free(tune->ti);
		free(path_copy);
		free(tune);
	}
}

int tune_in_playlist(struct tune *tune) {
	int i;

	if (tune) {
		for (i = 0; i < playlistcount; i++)
			if (tune->path == playlist[i]->path)
				return true;
	}
	return false;
}

int tune_in_displaylist(struct tune *tune) {
	int i;

	if (tune) {
		for (i = 0; i < displaycount; i++)
			if (tune->path == displaytunes[i]->path)
				return true;
	}
	return false;
}

/* -------------------------------------------------------------------------- */

struct tune *find_next_song(struct tune *tune) {
	int i, j;

	/*
	 * Is the current playing song in the now loaded playlist?
	 */
	for (i = 0; i < playlistcount - 1; i++) {
		if (tune->path == playlist[i]->path)
			for (j = i + 1; j < playlistcount - 1; j++)
				if (playlist[j]->search)
					return playlist[j];
	}

	/*
	 * Is the current playing song on the screen?
	 */
	for (i = 0; i < displaycount - 1; i++) {
		if (tune->path == displaytunes[i]->path)
			for (j = i + 1; j < displaycount - 1; j++)
				if (displaytunes[j]->search)
					return displaytunes[j];
	}

	/*
	 * Nope, try to find it in the biglist
	 */
	for (i = 0; i < allcount - 1; i++) {
		if (tune->path == alltunes[i].path)
			for (j = i + 1; j < allcount - 1; j++)
				if (alltunes[j].search)
					return &alltunes[j];
	}

	/*
	 * OK, nothing found... start from the beginning!
	 */
	return &alltunes[0];
}

/* -------------------------------------------------------------------------- */

size_t filesize(const char *filename) {
	struct stat ss;
	int error;

	error = stat(filename, &ss);
	return error ? -1 : ss.st_size;
}

struct stat st;

void make_local_copy_of_database(int showprogress) {
#define COPY_SIZE 0x4000
	int i;
	char srcfilename[255];
	char dstfilename[255];
	bool mustcopy = false;
	int read_fd;
	int write_fd;
	char *buf;
	size_t bytes;
	double bytes_total = 0.0;
	double bytes_written = 0.0;
	time_t timeprogress = 0;
	time_t now;
	const char *data_dir = config_get_data_dir();

	if (stat(data_dir, &st) != 0) {
		printf("Error: Could not read path \"%s\"\n", data_dir);
		exit(-1);
	}

	for (i = 0; i < 5; i++) {
		snprintf(srcfilename, sizeof(srcfilename), "%s%d.db", data_dir, i);
		snprintf(dstfilename, sizeof(dstfilename), "%s%d.db", playlist_dir, i);
		bytes = filesize(srcfilename);
		if (bytes != (size_t)-1)
			bytes_total += (double)bytes;
		if (bytes != filesize(dstfilename))
			mustcopy = true;
	}
	if (!mustcopy)
		return;

	for (i = 0; i < 5; i++) {
		snprintf(srcfilename, sizeof(srcfilename), "%s%d.db", data_dir, i);
		snprintf(dstfilename, sizeof(dstfilename), "%s%d.db.tmp", playlist_dir, i);

		read_fd = open(srcfilename, O_RDONLY);
		if (read_fd < 0)
			continue;

		write_fd = open(dstfilename, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if (write_fd != -1) {
			buf = malloc(COPY_SIZE);
			if (!buf) {
				close(write_fd);
				close(read_fd);
				continue;
			}
			ssize_t chunk_read;
			while ((chunk_read = read(read_fd, buf, COPY_SIZE)) > 0) {
				ssize_t written_total = 0;
				while (written_total < chunk_read) {
					size_t remaining = (size_t)(chunk_read - written_total);
					ssize_t chunk_written
					    = write(write_fd, buf + written_total, remaining);
					if (chunk_written <= 0) {
						written_total = -1;
						break;
					}
					written_total += chunk_written;
					bytes_written += (double)chunk_written;
				}
				if (written_total < 0)
					break;
				if (showprogress) {
					now = time(NULL);
					if (now > timeprogress) {
						char progress[61];
						size_t filled = 0;

						if (bytes_total > 0.0) {
							double ratio = bytes_written / bytes_total;
							if (ratio < 0.0)
								ratio = 0.0;
							if (ratio > 1.0)
								ratio = 1.0;
							filled = (size_t)(ratio * 60.0);
						}
						const size_t progress_len = sizeof(progress) - 1;
						if (filled > progress_len)
							filled = progress_len;

						for (size_t idx = 0; idx < filled; idx++)
							progress[idx] = '#';
						for (size_t idx = filled; idx < progress_len; idx++)
							progress[idx] = ' ';
						progress[progress_len] = '\0';

						fprintf(stderr, "[%s]\r", progress);
						timeprogress = now;
					}
				}
			}
			free(buf);
			close(write_fd);
		}

		close(read_fd);
	}

	for (i = 0; i < 5; i++) {
		snprintf(srcfilename, sizeof(srcfilename), "%s%d.db.tmp", playlist_dir, i);
		snprintf(dstfilename, sizeof(dstfilename), "%s%d.db", playlist_dir, i);
		rename(srcfilename, dstfilename);
	}
}

/* -------------------------------------------------------------------------- */

/*
 * Equivalent to a malloc:
 * memblock= mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
 * And free:
 * munmap(memblock, len);
 */

void load_all_songs(void) {
	int count = 0;
	struct db_track **tracks = db_get_all_tracks(&count);

	if (!tracks) {
		return;
	}

	/* If database is empty, count will be 0 but tracks array is valid */
	if (count == 0) {
		free(tracks);
		return;
	}

	/* Allocate memory for alltunes array */
	alltunes = malloc(sizeof(struct tune) * count);
	if (!alltunes) {
		db_free_track_list(tracks, count);
		return;
	}

	allcount = count;

	/* Convert database tracks to tune structures */
	for (int i = 0; i < count; i++) {
		struct db_track *db_track = tracks[i];

		alltunes[i].path = strdup(db_track->filepath);
		alltunes[i].display = strdup(db_track->display_name);
		alltunes[i].search = strdup(db_track->search_text);
		alltunes[i].ti = malloc(sizeof(struct tuneinfo));
		memcpy(alltunes[i].ti, &db_track->ti, sizeof(struct tuneinfo));

		db_free_track(db_track);
	}

	free(tracks);

	/*
	 * Init the array used for (somewhat) quick linear searches
	 */
	for (int i = 0; i < 256; i++) {
		qsearch[i].lo = -1;
		qsearch[i].hi = -1;
	}
	for (int i = 0; i < allcount; i++) {
		int ch = 0xff & alltunes[i].search[0];
		if (-1 == qsearch[ch].lo)
			qsearch[ch].lo = i;
		qsearch[ch].hi = i + 1;
	}
}

/* -------------------------------------------------------------------------- */

char *genre_names[256] = {
	/*
	 * NOTE: The spelling of these genre names is identical to those found in
	 * Winamp and mp3info.
	 */
	"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz",
	"Metal", "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno",
	"Industrial", "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno",
	"Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical",
	"Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "Alt. Rock",
	"Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
	"Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance",
	"Dream", "Southern Rock", "Comedy", "Cult", "Gangsta Rap", "Top 40", "Christian Rap",
	"Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave", "Psychedelic", "Rave",
	"Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro",
	"Musical", "Rock & Roll", "Hard Rock", "Folk", "Folk/Rock", "National Folk", "Swing",
	"Fast-Fusion", "Bebob", "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
	"Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock",
	"Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson", "Opera",
	"Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire",
	"Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul",
	"Freestyle", "Duet", "Punk Rock", "Drum Solo", "A Cappella", "Euro-House", "Dance Hall",
	"Goa", "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie", "BritPop", "Negerpunk",
	"Polsk Punk", "Beat", "Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover",
	"Contemporary Christian", "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime",
	"JPop", "Synthpop"
};

/*
 * Return the name of the genre. O(1)
 */
char *genrename(int genre) {
	char *p = genre_names[genre & 0xff];
	return p ? p : _("(unknown)");
}

/* -------------------------------------------------------------------------- */

static bool is_all_digits(const char *s) {
	if (!*s)
		return false;
	for (; *s; s++) {
		/*
		 * Ignore newlines. Simplifies fgets() calls
		 */
		if ('\n' == *s)
			continue;
		if (!isdigit(*s))
			return false;
	}
	return true;
}

/* -------------------------------------------------------------------------- */

/*
 * The hash stuff is taken from
 * Jon Bentley's wonderful "Programming Pearls" book.
 */

struct hashnode {
	char *word;
	int count;
	int duration;
	struct hashnode *next;
};

#define NHASH 997
#define MULT 31
struct hashnode **hashnodebin = NULL;

void hash_init(void) {
	hashnodebin = malloc(NHASH * sizeof(struct hashnode));
	memset(hashnodebin, 0, NHASH * sizeof(struct hashnode));
}

void hash_done(void) {
	int i;
	struct hashnode *wp;

	for (i = 0; i < NHASH; i++) {
		for (wp = hashnodebin[i]; wp; wp = wp->next) {
			free(wp->word);
			free(wp);
		}
	}

	free(hashnodebin);
	hashnodebin = NULL;
}

/*
 * Our hash function maps a string to a positive integer less than NHASH:
 * Using unsigned integers ensures that h remains positive.
 */
unsigned int hash_calc(const char *s, int len) {
	unsigned int h = 0;
	int loops;

	loops = (len + 7) / 8; /* count > 0 assumed */
	switch (len % 8) {
	case 0:
		do {
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 7:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 6:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 5:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 4:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 3:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 2:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		case 1:
			h = MULT * h + tolowerarray[(unsigned char)*s++];
		} while (--loops > 0);
	}

	return h % NHASH;
}

/*
 * The work is done by incword, which increments the count associated with the
 * input word (and initializes it if it is not already there):
 */

void hash_incword(const char *s, int duration) {
	unsigned int h;
	struct hashnode *wp;
	int len;

	len = strlen(s);
	h = hash_calc(s, len);
	for (wp = hashnodebin[h]; wp; wp = wp->next) {
		if (0 == strncasecmp(s, wp->word, len)) {
			wp->count++;
			wp->duration += duration;
			return;
		}
	}

	wp = malloc(sizeof(struct hashnode));
	wp->count = 1;
	wp->duration = duration;

	/*
	 * inline-version of strndup.
	 */
	wp->word = malloc(len + 1);
	wp->word[len] = '\0';
	memcpy(wp->word, s, len);

	wp->next = hashnodebin[h];
	hashnodebin[h] = wp;
}

/* -------------------------------------------------------------------------- */

int can_open(const char *filename) {
	int error;

	/*
	 * Test if that file exist...
	 * That is, if the user has READ access rights.
	 */
	error = access(filename, R_OK);
	return !error;
}

/* ------------------------------------------------------------------------- */
/*                                                                           */
/*              SCREEN STUFF                                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

void make_ui(void) {
	/*
	 * Initialize the screen.
	 */
	initscr();
	nonl();
	cbreak();
	noecho();
	raw();

	/*
	 * Setup theme colors from config.
	 */
	if (has_colors()) {
		start_color();

		/* Try to use RGB colors if terminal supports it */
		if (can_change_color() && COLORS >= 256) {
/* Define custom colors using theme RGB values (scaled to 0-1000) */
#define RGB_SCALE(val) ((val) * 1000 / 255)

/* Custom color indices (use high numbers to avoid conflicts) */
#define THEME_MAIN_BG 16
#define THEME_MAIN_FG 17
#define THEME_ACCENT_BG 18
#define THEME_ACCENT_FG 19
#define THEME_PLAYING 20
#define THEME_PLAYLIST 21
#define THEME_HIGHLIGHT_BG 22
#define THEME_HIGHLIGHT_FG 23

			init_color(THEME_MAIN_BG, RGB_SCALE(global_config.theme.main_bg.r),
			    RGB_SCALE(global_config.theme.main_bg.g),
			    RGB_SCALE(global_config.theme.main_bg.b));
			init_color(THEME_MAIN_FG, RGB_SCALE(global_config.theme.main_fg.r),
			    RGB_SCALE(global_config.theme.main_fg.g),
			    RGB_SCALE(global_config.theme.main_fg.b));
			init_color(THEME_ACCENT_BG, RGB_SCALE(global_config.theme.accent_bg.r),
			    RGB_SCALE(global_config.theme.accent_bg.g),
			    RGB_SCALE(global_config.theme.accent_bg.b));
			init_color(THEME_ACCENT_FG, RGB_SCALE(global_config.theme.accent_fg.r),
			    RGB_SCALE(global_config.theme.accent_fg.g),
			    RGB_SCALE(global_config.theme.accent_fg.b));
			init_color(THEME_PLAYING, RGB_SCALE(global_config.theme.playing.r),
			    RGB_SCALE(global_config.theme.playing.g),
			    RGB_SCALE(global_config.theme.playing.b));
			init_color(THEME_PLAYLIST, RGB_SCALE(global_config.theme.playlist.r),
			    RGB_SCALE(global_config.theme.playlist.g),
			    RGB_SCALE(global_config.theme.playlist.b));
			init_color(THEME_HIGHLIGHT_BG,
			    RGB_SCALE(global_config.theme.highlight_bg.r),
			    RGB_SCALE(global_config.theme.highlight_bg.g),
			    RGB_SCALE(global_config.theme.highlight_bg.b));
			init_color(THEME_HIGHLIGHT_FG,
			    RGB_SCALE(global_config.theme.highlight_fg.r),
			    RGB_SCALE(global_config.theme.highlight_fg.g),
			    RGB_SCALE(global_config.theme.highlight_fg.b));

			/* Main window */
			init_pair(1, THEME_MAIN_FG, THEME_MAIN_BG);
			/* Info/status bars */
			init_pair(2, THEME_ACCENT_FG, THEME_ACCENT_BG);
			/* Now playing (not highlighted) */
			init_pair(3, THEME_PLAYING, THEME_MAIN_BG);
			/* Now playing (highlighted) */
			init_pair(4, THEME_PLAYING, THEME_ACCENT_BG);
			/* Highlighted item */
			init_pair(5, THEME_HIGHLIGHT_FG, THEME_HIGHLIGHT_BG);
			/* Playlist item (not highlighted) */
			init_pair(6, THEME_PLAYLIST, THEME_MAIN_BG);
			/* Playlist item (highlighted) */
			init_pair(7, THEME_PLAYLIST, THEME_ACCENT_BG);
		} else {
			/* Fallback to standard colors for limited terminals */
			init_pair(1, COLOR_WHITE, COLOR_BLACK);
			init_pair(2, COLOR_WHITE, COLOR_BLUE);
			init_pair(3, COLOR_YELLOW, COLOR_BLACK);
			init_pair(4, COLOR_YELLOW, COLOR_BLUE);
			init_pair(5, COLOR_BLACK, COLOR_WHITE);
			init_pair(6, COLOR_GREEN, COLOR_BLACK);
			init_pair(7, COLOR_GREEN, COLOR_BLUE);
		}
	}

	middlesize = LINES - 3;

	win_top = newwin(1, 0, 0, 0);
	win_info = newwin(1, 0, 1, 0);
	win_middle = newwin(middlesize, 0, 2, 0);
	win_bottom = newwin(1, 0, LINES - 1, 0);

	keypad(win_top, true);
}

void draw_scrollbar(void) {
	char scrollbar[255]; /* There is room for a 255-row display... */
	memset(scrollbar, ' ', sizeof(scrollbar));

	if (displaycount > 0 && middlesize > 0) {
		double ratio = (double)tunenr / (double)displaycount;
		if (ratio < 0.0)
			ratio = 0.0;
		else if (ratio > 1.0)
			ratio = 1.0;

		int64_t marker_limit = (int64_t)middlesize - 1;
		int64_t limit = (int64_t)sizeof(scrollbar) - 1;
		if (marker_limit > limit)
			marker_limit = limit;
		if (marker_limit < 0)
			marker_limit = 0;
		if (marker_limit > INT_MAX)
			marker_limit = INT_MAX;
		int max_marker = (int)marker_limit;

		double scaled = ratio * (double)max_marker;
		if (scaled < 0.0)
			scaled = 0.0;
		if (scaled > (double)max_marker)
			scaled = (double)max_marker;
		long rounded = lround(scaled);
		if (rounded < 0)
			rounded = 0;
		if (rounded > max_marker)
			rounded = max_marker;
		const int marker = (int)rounded;
		scrollbar[marker] = '#';
	}

	for (int row = 0; row < middlesize; row++) {
		char cell = (row < (int)sizeof(scrollbar)) ? scrollbar[row] : ' ';
		mvwaddch(win_middle, row, COLS - 1, cell);
	}
}

/*
 * Get the length of a UTF-8 character sequence starting at the given byte
 * Returns 1-4 for valid sequences, 1 for invalid bytes
 */
static inline int utf8_char_len_ui(unsigned char c) {
	if ((c & 0x80) == 0x00)
		return 1; /* 0xxxxxxx - ASCII */
	if ((c & 0xE0) == 0xC0)
		return 2; /* 110xxxxx - 2-byte */
	if ((c & 0xF0) == 0xE0)
		return 3; /* 1110xxxx - 3-byte */
	if ((c & 0xF8) == 0xF0)
		return 4; /* 11110xxx - 4-byte */
	return 1; /* Invalid UTF-8, treat as single byte */
}

/*
 * Find a safe UTF-8 boundary at or before the given byte offset
 * Returns adjusted offset that doesn't split a multi-byte character
 */
static size_t utf8_safe_offset(const char *str, size_t offset) {
	if (offset == 0 || str[offset] == '\0')
		return offset;

	/* Walk backwards to find start of UTF-8 character */
	while (offset > 0 && ((unsigned char)str[offset] & 0xC0) == 0x80) {
		offset--; /* Continuation byte (10xxxxxx) */
	}

	return offset;
}

/*
 * Truncate string at safe UTF-8 boundary without splitting characters
 * Returns actual length after truncation
 */
static size_t utf8_safe_truncate(char *str, size_t max_bytes) {
	if (!str || max_bytes == 0)
		return 0;

	size_t len = strlen(str);
	if (len <= max_bytes) {
		return len;
	}

	/* Find safe truncation point */
	size_t safe_len = utf8_safe_offset(str, max_bytes);
	str[safe_len] = '\0';
	return safe_len;
}

/*
 * Translate "aaa/bbb/ccc/ddd"
 * to        "aaa - bbb - ccc - ddd"
 * UTF-8 safe version that handles substrings correctly
 */
static void depath(char *dst, size_t dst_size, const char *src) {
	if (!dst || dst_size == 0) {
		return;
	}
	dst[0] = '\0';

	/* If src is null or empty, nothing to do */
	if (!src || !*src) {
		return;
	}

	/* Find start of first complete UTF-8 character in substring */
	const char *start = src;
	while (*start) {
		/* Check if this byte starts a valid UTF-8 sequence */
		unsigned char c = (unsigned char)*start;
		if ((c & 0x80) == 0x00)
			break; /* ASCII character */
		if ((c & 0xE0) == 0xC0)
			break; /* 2-byte start */
		if ((c & 0xF0) == 0xE0)
			break; /* 3-byte start */
		if ((c & 0xF8) == 0xF0)
			break; /* 4-byte start */

		/* This is a continuation byte, skip it */
		start++;
		if (!*start) {
			return;
		}
	}

	/* If we reached end without finding a valid start, nothing to do */
	if (!*start)
		return;

	char *write = dst;
	size_t remaining = dst_size - 1; /* Reserve space for terminator */

	/* Now process from the safe starting point */
	while (*start && remaining > 0) {
		if (*start != '/') {
			/* Copy complete UTF-8 character */
			int char_len = utf8_char_len_ui((unsigned char)*start);
			for (int i = 0; i < char_len && start[i] && remaining > 0; i++) {
				*write++ = start[i];
				remaining--;
			}
			start += char_len;
		} else {
			if (remaining < 3)
				break; /* Not enough room for separator */
			*write++ = ' ';
			*write++ = '-';
			*write++ = ' ';
			remaining -= 3;
			start++;
		}
	}
	*write = '\0';
}

static void depath_append(char *dst, size_t dst_capacity, const char *src) {
	if (!dst || dst_capacity == 0)
		return;

	size_t prefix_len = strnlen(dst, dst_capacity);
	if (prefix_len >= dst_capacity - 1) /* No room to append */
		return;

	depath(dst + prefix_len, dst_capacity - prefix_len, src);
}

void draw_one_song(int row, int item, int highlight) {
#define FILLER ' '
	struct tune *tune;
	char buf[512];
	int colorpair;
	int hours;
	struct tm tm;
	char *p;

	tune = displaytunes[item];

	memset(buf, FILLER, sizeof(buf));
	buf[0] = 0;

	switch (sort_arg) {
	case ARG_NORMAL:
		break;
	case ARG_LENGTH:
		hours = tune->ti->duration / 60;
		if (hours > 999)
			snprintf(buf, sizeof(buf), "%6d ", hours);
		else
			snprintf(buf, sizeof(buf), "%3d:%02d ", hours, tune->ti->duration % 60);
		break;
	case ARG_SIZE:
		snprintf(buf, sizeof(buf), "%9d ", tune->ti->filesize);
		break;
	case ARG_DATE:
		localtime_r(&tune->ti->filedate, &tm);
		snprintf(buf, sizeof(buf), "%4d-%02d-%02d ", 1900 + tm.tm_year, 1 + tm.tm_mon,
		    tm.tm_mday);
		break;
	case ARG_BITRATE:
		snprintf(buf, sizeof(buf), "%3d ", tune->ti->bitrate);
		break;
	case ARG_GENRE:
		snprintf(buf, sizeof(buf), "%-18s ", genrename(tune->ti->genre));
		break;
	case ARG_RATING:
		switch (tune->ti->rating) {
		case 1:
			safe_strcat(buf, "*     ", sizeof(buf));
			break;
		case 2:
			safe_strcat(buf, "**    ", sizeof(buf));
			break;
		case 3:
			safe_strcat(buf, "***   ", sizeof(buf));
			break;
		case 4:
			safe_strcat(buf, "****  ", sizeof(buf));
			break;
		case 5:
			safe_strcat(buf, "***** ", sizeof(buf));
			break;
		default:
			safe_strcat(buf, "      ", sizeof(buf));
			break;
		}
		break;
#ifdef USE_FINISH
	case ARG_FINISH:
		time_t t = displaytimes[item] + started_playing_time; /*time(NULL) */
		localtime_r(&t, &tm);
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
		break;
#endif
	}

	const int step = col_step;
	if (ARG_PATH == sort_arg) {
		const size_t path_len = strlen(tune->path);
		/* Use UTF-8 safe offset for horizontal scrolling */
		size_t safe_step = (step < 0 || (size_t)step > path_len)
		    ? 0
		    : utf8_safe_offset(tune->path, step);
		if (safe_step > path_len)
			safe_step = path_len;
		p = tune->path + safe_step;

		/* UTF-8 safe copy - copy up to COLS characters, not bytes */
		size_t copied = 0;
		size_t remaining = COLS;
		while (*p && copied < remaining) {
			int char_len = utf8_char_len_ui((unsigned char)*p);
			if ((size_t)char_len > remaining)
				break; /* Don't split character */

			for (int i = 0; i < char_len && *p; i++) {
				buf[strlen(buf)] = *p++;
			}
			copied += char_len;
		}
	} else {
		const size_t display_len = strlen(tune->display);
		/* Use UTF-8 safe offset for horizontal scrolling */
		size_t safe_step = (step < 0 || (size_t)step > display_len)
		    ? 0
		    : utf8_safe_offset(tune->display, step);
		if (safe_step > display_len)
			safe_step = display_len;
		p = tune->display + safe_step;
		depath_append(buf, sizeof(buf), p);
	}

	/* UTF-8 safe truncation at screen width */
	size_t buf_len = utf8_safe_truncate(buf, COLS);
	/* Fill remaining space with FILLER */
	while (buf_len < (size_t)COLS) {
		buf[buf_len++] = FILLER;
	}
	buf[COLS] = 0;

	/*
	 * Show markers if we're horizontal scrolling
	 */
	if (col_step) {
		buf[0] = '<';
		if (COLS >= 2)
			buf[COLS - 2] = '>';
	}

	/*
	 * Find the color of the string
	 */
	if (tune && now_playing_tune && tune->path == now_playing_tune->path)
		colorpair = highlight ? COLOR_PAIR(4) : COLOR_PAIR(3);
	else if (tune_in_playlist(tune))
		colorpair = highlight ? COLOR_PAIR(7) : COLOR_PAIR(6);
	else if (highlight)
		colorpair = COLOR_PAIR(5);
	else
		colorpair = 0;

	if (colorpair)
		wattron(win_middle, colorpair);
	mvwaddstr(win_middle, row, 0, buf);
	if (colorpair)
		wattroff(win_middle, colorpair);
}

void draw_bottom_FXX_help(const char *key, const char *help) {
	wstandout(win_bottom);
	waddstr(win_bottom, key);
	wstandend(win_bottom);
	waddstr(win_bottom, help);
}

void draw_centered(WINDOW *w, int row, const char *format, ...) {
	va_list va;
	char buf[1024];

	va_start(va, format);
	vsnprintf(buf, sizeof(buf), format, va);
	va_end(va);

	mvwaddstr(w, row, (COLS - strlen(buf)) / 2, buf);
}

void refresh_screen(void) {
	static bool show_splash = true;
	int row;

	/*
	 * top
	 */
	werase(win_top);
	mvwaddstr(win_top, 0, 0, search_string);

	/*
	 * info
	 */
	wbkgd(win_info, COLOR_PAIR(2));
	werase(win_info);
	if (now_playing_tune)
		mvwaddstr(win_info, 0, 0, now_playing_tune->display);

	/*
	 * middle
	 */
	wbkgd(win_middle, COLOR_PAIR(1));
	werase(win_middle);
	if (show_splash) {
		draw_centered(
		    win_middle, 3, "  ________.__                .__                     ");
		draw_centered(
		    win_middle, 4, " /  _____/|  | _____    ____ |__| ________________   ");
		draw_centered(
		    win_middle, 5, "/   \\  ___|  | \\__  \\ _/ ___\\|  |/ __ \\_  __ \\__  \\  ");
		draw_centered(win_middle, 6,
		    "\\    \\_\\  \\  |__/ __ \\\\  \\___|  \\  ___/|  | \\/\\/ __ \\_");
		draw_centered(
		    win_middle, 7, " \\______  /____(____  /\\___  >__|\\___  >__|  (____  /");
		draw_centered(
		    win_middle, 8, "        \\/          \\/     \\/        \\/           \\/  ");
		draw_centered(win_middle, 10, "- Heavy Duty Jukebox -");
		draw_centered(win_middle, 11, complete_version());
		draw_centered(win_middle, 12, "Copyright (c) Plux Stahre 2025");
		draw_centered(win_middle, 14, _("%d songs in database"), allcount);
		show_splash = false;
	} else {
		for (row = 0; row < middlesize; row++) {
			int64_t display_index = (int64_t)row + (int64_t)toptunenr;
			if (display_index < 0 || display_index > INT_MAX)
				break;
			if (display_index >= (int64_t)displaycount)
				break;
			int display_idx = (int)display_index;
			draw_one_song(row, display_idx, display_idx == tunenr);
		}
		draw_scrollbar();
	}

	/*
	 * bottom
	 */
	wbkgd(win_bottom, COLOR_PAIR(2));
	werase(win_bottom);
	wmove(win_bottom, 0, 0);
	draw_bottom_FXX_help("F1", _("Info"));
	draw_bottom_FXX_help("F2", _("View"));
	draw_bottom_FXX_help("F3", _("Sort"));
	draw_bottom_FXX_help("F4", _("Context"));
	draw_bottom_FXX_help("F5", _("ShowPL"));
	draw_bottom_FXX_help("F6", _("LoadPL"));
	draw_bottom_FXX_help("F7", _("SavePL"));
	draw_bottom_FXX_help("F8", _("Burn"));
	draw_bottom_FXX_help("F9", _("Time"));
#ifdef USE_BACK
	draw_bottom_FXX_help("F11", _("Back"));
#endif
	draw_bottom_FXX_help("+*", _("Tag"));

	wnoutrefresh(win_top);
	wnoutrefresh(win_info);
	wnoutrefresh(win_middle);
	wnoutrefresh(win_bottom);
}

void user_move_cursor(int delta, int redraw) {
	int count;

	count = abs(delta);
	int step = (delta < 0) ? -1 : +1;

	while (count--) {
		if (redraw) {
			int64_t relative_before = (int64_t)tunenr - (int64_t)toptunenr;
			int draw_index = 0;
			if (relative_before > INT_MAX)
				draw_index = INT_MAX;
			else if (relative_before < INT_MIN)
				draw_index = INT_MIN;
			else
				draw_index = (int)relative_before;
			draw_index
			    = clamp_int(draw_index, 0, (middlesize > 0) ? middlesize - 1 : 0);
			draw_one_song(draw_index, tunenr, false);
		}

		int64_t new_tunenr = (int64_t)tunenr + step;
		if (new_tunenr > INT_MAX)
			new_tunenr = INT_MAX;
		if (new_tunenr < INT_MIN)
			new_tunenr = INT_MIN;

		int64_t max_index = (displaycount > 0) ? ((int64_t)displaycount - 1) : 0;
		if (new_tunenr > max_index)
			new_tunenr = max_index;
		if (new_tunenr < 0)
			new_tunenr = 0;

		tunenr = (int)new_tunenr;

		int64_t relative = 0;
		if (ckd_sub(&relative, (int64_t)tunenr, (int64_t)toptunenr) != 0)
			relative = (tunenr >= toptunenr) ? INT64_MAX : INT64_MIN;
		if (relative >= (int64_t)middlesize || relative < 0) {
			int64_t new_top = (int64_t)toptunenr + step;
			if (new_top > INT_MAX)
				new_top = INT_MAX;
			if (new_top < INT_MIN)
				new_top = INT_MIN;
			toptunenr = (int)new_top;
			if (toptunenr < 0)
				toptunenr = 0;
			if (displaycount > 0) {
				int64_t top_limit
				    = ((int64_t)displaycount > 0) ? ((int64_t)displaycount - 1) : 0;
				if (top_limit < 0)
					top_limit = 0;
				if (top_limit > INT_MAX)
					top_limit = INT_MAX;
				int max_top = (int)top_limit;
				if (toptunenr > max_top)
					toptunenr = max_top;
			}

			if (redraw) {
				scrollok(win_middle, true);
				wscrl(win_middle, step);
				scrollok(win_middle, false);
			}
		}

		if (redraw) {
			int64_t relative_after = (int64_t)tunenr - (int64_t)toptunenr;
			int draw_index = 0;
			if (relative_after > INT_MAX)
				draw_index = INT_MAX;
			else if (relative_after < INT_MIN)
				draw_index = INT_MIN;
			else
				draw_index = (int)relative_after;
			draw_index
			    = clamp_int(draw_index, 0, (middlesize > 0) ? middlesize - 1 : 0);
			draw_one_song(draw_index, tunenr, true);
			draw_scrollbar();
		}
	}
}

void after_move(void) {
	/* UTF-8 aware cursor positioning */
	size_t char_pos = 0;
	for (int i = 0; search_string[i];) {
		int char_len = utf8_char_len_ui((unsigned char)search_string[i]);
		i += char_len;
		char_pos++;
	}
	wmove(win_top, 0, char_pos);
	wnoutrefresh(win_top);
}

void update_searchstring(void) {
	werase(win_top);
	mvwaddstr(win_top, 0, 0, search_string);
}

/*
 * Trigger search if search string has changed (real-time filtering)
 */
void trigger_realtime_search(void) {
	if (0 != strcmp(search_string, last_search_string)) {
		do_search();
		safe_strcpy(last_search_string, search_string, sizeof(last_search_string));
		refresh_screen();
	}
}

void show_info(char *format, ...) {
	va_list va;

	va_start(va, format);
	werase(win_info);
	vwprintw(win_info, format, va);
	va_end(va);

	wnoutrefresh(win_info);
	doupdate();
	showprogressagain = time(NULL) + 5;
}

/* --------------------- E N D  O F  S C R E E N  S T U F F ---------------- */

void append_tune_to_history(struct tune *tune, time_t timestarted) {
	time_t t;
	struct tm tm;
	char historyfilename[512];
	FILE *f;

	t = time(NULL);
	if (!timestarted)
		timestarted = t;
	localtime_r(&t, &tm);
	snprintf(historyfilename, sizeof(historyfilename), "%s%4d_%02d_%02d.list", playlist_dir,
	    1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday);
	f = fopen(historyfilename, "a");
	if (f) {
		fprintf(f, "%s\n", tune->display);
		fprintf(f, "%d\n", (int)timestarted);
		fclose(f);
	}
}

/* -------------------------------------------------------------------------- */

void save_playlist(const char *playlistname) {
	FILE *f;
	int i;
	char filename[512];

	safe_strcpy(latest_playlist_name, playlistname, sizeof(latest_playlist_name));

	if (!safe_path_join(filename, sizeof(filename), playlist_dir, playlistname)) {
		show_info(_("Error: playlist path too long"));
		return;
	}
	if (!strstr(filename, ".list"))
		safe_strcat(filename, ".list", sizeof(filename));
	f = fopen(filename, "w");
	if (f) {
		for (i = 0; i < playlistcount; i++)
			fprintf(f, "%s\n", playlist[i]->display);
		fclose(f);
	}
}

/* -------------------------------------------------------------------------- */

int alltunes_sort(const void *a, const void *b) {
	return strcasecmp(((struct tune *)a)->display, ((struct tune *)b)->display);
}

void store_to_cache(const char *badname, const char *goodname) {
	char cachefilename[512];
	FILE *f;

	snprintf(
	    cachefilename, sizeof(cachefilename), "%sbad-name-to-good-name.cache", playlist_dir);
	f = fopen(cachefilename, "a");
	if (f) {
		fprintf(f, "%s\n", badname);
		fprintf(f, "%s\n", goodname);
		fclose(f);
	}
}

struct tune *get_from_cache(const char *badname) {
	char cachefilename[512];
	FILE *f;
	char buf[1024];
	struct tune *tune = NULL;
	struct tune key;

	snprintf(
	    cachefilename, sizeof(cachefilename), "%sbad-name-to-good-name.cache", playlist_dir);
	f = fopen(cachefilename, "r");
	if (!f)
		return NULL;

	while (fgets(buf, sizeof(buf), f)) {
		chop(buf);
		if (0 == strcmp(badname, buf)) {
			if (!fgets(buf, sizeof(buf), f))
				break;
			chop(buf);
			key.display = buf;
			tune = bsearch(
			    &key, (void *)alltunes, allcount, sizeof(struct tune), alltunes_sort);
			if (tune)
				break;
		} else {
			if (!fgets(buf, sizeof(buf), f))
				break;
		}
	}
	fclose(f);

	return tune;
}

struct tune *find_tune_by_displayname_harder(const char *buf) {
	int ch;
	int i;

	ch = buf[0];
	for (i = qsearch[ch].lo; i < qsearch[ch].hi; i++) {
		if (0 == strcmp(alltunes[i].search, buf))
			return &alltunes[i];
	}

	return NULL;
}

struct tune *find_tune_by_displayname(char *displayname, int hardness) {
	struct tune *tune;
	struct tune key;
	int i;
	char buf[1024];
	int bestresult;
	int res;
	int ch;

	key.display = displayname;
	tune = bsearch(&key, (void *)alltunes, allcount, sizeof(struct tune), alltunes_sort);

	if (!tune && hardness > 0) {
		tune = get_from_cache(displayname);
		if (!tune) {
			safe_strcpy(buf, displayname, sizeof(buf));
			only_searchables(buf);
			tune = find_tune_by_displayname_harder(buf);

			if (!tune && hardness > 1) {
				bestresult = -1;
				ch = buf[0];
				for (i = qsearch[ch].lo; i < qsearch[ch].hi; i++) {
					res = fuzzy(alltunes[i].search, buf);
					if (res > 50 && res > bestresult) {
						tune = &alltunes[i];
						bestresult = res;
					}
				}
			}

			if (tune)
				store_to_cache(displayname, tune->display);
		}
	}

	return tune;
}

/* -------------------------------------------------------------------------- */

void add_tune_to_playlist(struct tune *tune) {
	if (!tune)
		return;

	/*
	 * Is this tune already in the list?
	 * Don't put it there again
	 */
	if (tune_in_playlist(tune))
		return;

	/*
	 * Make room for one more song in the playlist and insert it.
	 */
	playlist = realloc(playlist, (playlistcount + 1) * sizeof(void *));
	playlist[playlistcount++] = tune;
}

/* -------------------------------------------------------------------------- */

void do_load_playlist(const char *playlistname) {
	FILE *f;
	char buf[1024];
	char filename[512];
	struct tune *tune;

	if (!safe_path_join(filename, sizeof(filename), playlist_dir, playlistname)) {
		show_info(_("Error: playlist path too long"));
		return;
	}
	if (!strstr(filename, ".list"))
		safe_strcat(filename, ".list", sizeof(filename));
	f = fopen(filename, "r");
	if (!f)
		return;

	playlistcount = 0;
	safe_strcpy(latest_playlist_name, playlistname, sizeof(latest_playlist_name));
	while (fgets(buf, sizeof(buf), f)) {
		/*
		 * Ignore the "this-song-was-played-on-this-date" line
		 */
		if (is_all_digits(buf))
			continue;

		chop(buf);
		tune = find_tune_by_displayname(buf, 2);
		if (!tune) {
			/*
			 * Oops! The song in the playlist was not found
			 * in the big list! Perhaps it has been renamed?
			 * Indicate with ???'s that this song _is_ in the
			 * list but not available at the moment.
			 */
			char buf2[1024];
			safe_strcpy(buf2, "??? ", sizeof(buf2));
			safe_strcat(buf2, buf, sizeof(buf2));

			tune = malloc(sizeof(struct tune));
			tune->path = strdup(buf2);
			tune->display = strdup(buf2);
			tune->search = EMPTY_SEARCH;
			tune->ti = malloc(sizeof(struct tuneinfo));
			tune->ti->filesize = 0;
			tune->ti->filedate = 0;
			tune->ti->duration = 0;
			tune->ti->bitrate = 0;
			tune->ti->genre = 0;
			tune->ti->rating = 0;
		}

		add_tune_to_playlist(tune);
	}
	fclose(f);
}

/* -------------------------------------------------------------------------- */

void precache_a_song(struct tune *tune) {
	int fd;
	int i;
	char buf[4096];

	if (!tune)
		return;

	/*
	 * Read the first two 4K pages of the file.
	 * This helps avoid disk I/O latency during playback transitions.
	 */
	fd = open(tune->path, O_RDONLY);
	if (-1 != fd) {
		for (i = 0; i < 2; i++) {
			if (read(fd, buf, sizeof(buf)) < 0)
				break;
		}
		close(fd);
	}
}

pthread_t cache_next_song_thread_id = 0;
void *cache_next_song_in_advance_thread(void *arg) {
	precache_a_song(find_next_song(arg));
	cache_next_song_thread_id = 0;
	return NULL;
}

pthread_t append_tune_to_history_thread_id = 0;
void *append_tune_to_history_thread(void *arg) {
	append_tune_to_history(arg, started_playing_time);
	tune_to_save_to_history = NULL;
	append_tune_to_history_thread_id = 0;
	return NULL;
}

pthread_t readahead_thread_id = 0;
int g_percentplayed = 0;
void *readahead_thread(void *arg) {
	struct tune *tune = arg;
	int fd;
	int percentplayed = g_percentplayed;
	char buf[4096];
	off_t readpos;
	int i;

	if (percentplayed < 100 && tune && tune->ti && tune->ti->filesize) {
		fd = open(tune->path, O_RDONLY);
		if (fd != -1) {
			readpos = (tune->ti->filesize / 100L) * percentplayed;
			if (-1 != lseek(fd, readpos, SEEK_SET)) {
				for (i = 0; i < 16; i++) {
					if (read(fd, buf, sizeof(buf)) < 0)
						break;
				}
			}
			close(fd);
		}
	}

	readahead_thread_id = 0;
	return NULL;
}

void parse_shoutcaststream_log(char *result) {
	FILE *f;
	char buf[1024];
	char *p;

	f = fopen(GLACIERA_PIPE, "r");
	if (!f)
		return;

	/*
	 * The logfile from mplayer contains lines looking like this;
	 * ICY Info: StreamTitle='Diskonnekted - Eternal [v2.0
	 * break]';StreamUrl='http://www.rantradio.com';
	 *                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	 *                        This is what we want, the title of the song
	 */
	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "StreamTitle")) { /* Don't localize */
			p = strstr(buf, "='");
			if (p) {
				safe_strcpy(result, p + 2, sizeof(result));
				p = strstr(result, "';");
				if (p)
					*p = '\0';
			}
		}
	}
	fclose(f);
}

void update_song_progress_handler(int sig) {
	(void)sig;
	char buf[255];
	int percentplayed;
	int secondsleft;
	int secondsplayed;
	char bar[1024];
	int barlength;

	if (in_input_mode)
		goto just_renew_timer;
	if (in_action)
		goto just_renew_timer;

#ifdef USE_FINISH
	if (ARG_FINISH == sort_arg) {
		refresh_screen();
		after_move();
		doupdate();
	}
#endif

	if (time(NULL) < showprogressagain)
		goto just_renew_timer;
	if (!now_playing_tune)
		goto just_renew_timer;
	if (paused)
		goto just_renew_timer;

	if (!started_playing_time)
		started_playing_time = time(NULL);
	secondsplayed = time(NULL) - started_playing_time;
	secondsleft = now_playing_tune->ti->duration - secondsplayed;
	if (now_playing_tune->ti->duration)
		percentplayed = (100 * secondsplayed) / now_playing_tune->ti->duration;
	else {
		/*
		 * Don't mess up the display for songs that has no duration
		 */
		percentplayed = 100;
		secondsleft = secondsplayed;
	}

	/*
	 * Sanity checks to avoid messing up the display.
	 * The maximium allowed songlength is 999 minutes (16 hours)
	 */
	if (percentplayed < 0)
		percentplayed = 0;
	if (percentplayed > 100)
		percentplayed = 100;
	if (secondsleft < 0)
		secondsleft = 0;
	if (secondsleft > 999 * 60)
		secondsleft = 999 * 60;

	barlength = COLS - 7;
	memset(bar, ' ', barlength);
	sprintf(&bar[barlength - 4], "%3d%%", percentplayed); /* Yes, use plain old sprintf() */
	memset(bar, '#', (percentplayed * barlength) / 100);

	/*
	 * When we're streaming, show the name of the
	 * current song instead of the #######-bar.
	 */
	if (!now_playing_tune->ti->duration)
		parse_shoutcaststream_log(bar);

	/*
	 * Limit the length of the bar so it doesn't go beyond the screen.
	 */
	bar[barlength] = 0;

	snprintf(buf, sizeof(buf), "%3d:%02d %s", secondsleft / 60, secondsleft % 60, bar);
	werase(win_info);
	mvwaddstr(win_info, 0, 0, buf);
	wnoutrefresh(win_info);
	after_move();
	doupdate();

	/*
	 * Pre-load the next song ten seconds before this song ends
	 */
	if (secondsleft < 10 && !cache_next_song_thread_id) {
		pthread_create(&cache_next_song_thread_id, &detachedattr,
		    &cache_next_song_in_advance_thread, now_playing_tune);
	}

	/*
	 * !!2005-05-12 KB
	 * Save to history if song has been played at least 50% of its length
	 * or for at least 240 seconds (4minutes)
	 */
	if (percentplayed >= 50 || secondsplayed >= 240) {
		if (tune_to_save_to_history && !append_tune_to_history_thread_id) {
			pthread_create(&append_tune_to_history_thread_id, &detachedattr,
			    &append_tune_to_history_thread, tune_to_save_to_history);
		}
	}

	/*
	 * !!2005-05-13 KB
	 * To avoid skips, cache a few seconds ahead from the
	 * current position in the file
	 */
	if (opt_read_ahead && !readahead_thread_id) {
		g_percentplayed = percentplayed;
		pthread_create(
		    &readahead_thread_id, &detachedattr, &readahead_thread, now_playing_tune);
	}

just_renew_timer:
	alarm(1);
}

/* -------------------------------------------------------------------------- */

void stop_playing(pid_t pid) {
	signal(SIGCHLD, SIG_IGN);

	/*
	 * !!2005-05-19KB
	 * Changed from SIGKILL to SIGTERM.
	 * Fixes the annoying SAMBA error messages on Red Hat 9.
	 */
	kill(pid, SIGTERM);

	waitpid(pid, NULL, 0);
}

void start_play(int userpressed_enter, struct tune *tune) {
	/* TODO: display only the n last characters if the string is long */
	if (userpressed_enter)
		show_info(_("Loading '%s'..."), tune->display);

	/*
	 * Cache (preload) the intro of the new song before we stop the
	 * current one to avoid embarrassing silences in the music flow.
	 */
	precache_a_song(tune);

	/*
	 * Now stop the current playing song.
	 */
	if (player_pid) {
		stop_playing(player_pid);
		player_pid = 0;
	}

	now_playing_tune = find_in_alltunes_by_display_pointer(tune->display);

	/* Use the path directly - UTF-8 is now handled properly throughout */
	if (!can_open(now_playing_tune->path)) {
		/*
		 * Yes, finally got rid of the ugly "execl-$JUST$PLAY$NEXT$SONG" hack.
		 */
		find_and_play_next_handler(0);
	} else {
		started_playing_time = time(NULL);
		tune_to_save_to_history = now_playing_tune;
		player_pid = fork();
		if (0 == player_pid) {
			/*
			 * Redirect the new process' stdout to /tmp/glaciera.stdout,
			 * so we can scan it for lines containing "StreamTitle".
			 */
			int out_fd
			    = open(GLACIERA_PIPE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
			if (out_fd < 0) {
				fprintf(stderr, "Failed to open %s: %s\n", GLACIERA_PIPE,
				    strerror(errno));
				_exit(EXIT_FAILURE);
			}
			if (dup2(out_fd, STDOUT_FILENO) == -1) {
				fprintf(stderr, "Failed to redirect stdout: %s\n", strerror(errno));
				close(out_fd);
				_exit(EXIT_FAILURE);
			}
			close(out_fd);

			int err_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
			if (err_fd < 0) {
				fprintf(stderr, "Failed to open /dev/null: %s\n", strerror(errno));
				_exit(EXIT_FAILURE);
			}
			if (dup2(err_fd, STDERR_FILENO) == -1) {
				fprintf(stderr, "Failed to redirect stderr: %s\n", strerror(errno));
				close(err_fd);
				_exit(EXIT_FAILURE);
			}
			close(err_fd);

			music_play(now_playing_tune->path);

			/*
			 * Only reached if, for some reason, the call to execl() fails
			 */
			exit(EXIT_FAILURE);
		}
	}

	/* Adding a new function, to tell other programs
	 * what we are playing.
	 * // Plux
	 */
	FILE *f;
	f = fopen("/tmp/glaciera-nowplaying", "w");
	if (f) {
		fprintf(f, "%s", tune->display);
		fclose(f);
	}

	signal(SIGCHLD, find_and_play_next_handler);
	update_song_progress_handler(0);
}

void find_and_play_next_handler(int dummyparam) {
	(void)dummyparam;
	if (wanna_quit)
		return;

	/*
	 * !!20080427 - Fix ugly segfault
	 */
	if (now_playing_tune)
		start_play(false, find_next_song(now_playing_tune));
	refresh_screen();
	after_move();
	doupdate();
}

/* -------------------------------------------------------------------------- */

int sort_sort_direction = +1;

/*
 * In sort_ARG_DATE() divide by both dates by 86400, (60*60*24)
 * to discard the hh:mm:ss part of the date since we
 * don't want the tunes sorted by "yyyy-mm-dd hh:mm:ss",
 * "yyyy-mm-dd" is enough.
 */
int sort_ARG_NORMAL(struct tune *a, struct tune *b) {
	if (needs_strcasecmp_sort)
		return strcasecmp(a->display, b->display);
	else
		return (BIGPTR)a->display - (BIGPTR)b->display;
}
int sort_ARG_LENGTH(struct tune *a, struct tune *b) {
	return a->ti->duration - b->ti->duration;
}
int sort_ARG_SIZE(struct tune *a, struct tune *b) {
	return a->ti->filesize - b->ti->filesize;
}
int sort_ARG_DATE(struct tune *a, struct tune *b) {
	return a->ti->filedate / 86400 - b->ti->filedate / 86400;
}
int sort_ARG_BITRATE(struct tune *a, struct tune *b) {
	return a->ti->bitrate - b->ti->bitrate;
}
int sort_ARG_GENRE(struct tune *a, struct tune *b) {
	return a->ti->genre - b->ti->genre;
}
int sort_ARG_RATING(struct tune *a, struct tune *b) {
	return a->ti->rating - b->ti->rating;
}
int sort_ARG_PATH(struct tune *a, struct tune *b) {
	return strcmp(a->path, b->path);
}
int sort_ARG_FINISH(struct tune *a, struct tune *b) {
	return sort_ARG_NORMAL(a, b);
}

int (*sort_funcs[])(struct tune *a, struct tune *b) = {
	&sort_ARG_NORMAL,
	&sort_ARG_LENGTH,
	&sort_ARG_SIZE,
	&sort_ARG_DATE,
	&sort_ARG_BITRATE,
	&sort_ARG_GENRE,
	&sort_ARG_RATING,
	&sort_ARG_PATH,
	&sort_ARG_FINISH,
};

int sort_sort_function(const void *a, const void *b) {
	struct tune *ta = *((struct tune **)a);
	struct tune *tb = *((struct tune **)b);
	int result;

	result = sort_funcs[sort_arg](ta, tb);

	/*
	 * We have a duplicate (on length, size or date).
	 */
	if (!result && (sort_arg != ARG_NORMAL))
		result = sort_ARG_NORMAL(ta, tb);

	return result * sort_sort_direction;
}

void do_sort_work(int asort_arg, int asort_direction) {
	int save_sort_arg = sort_arg;
	int save_sort_sort_direction = sort_sort_direction;

	sort_arg = asort_arg;
	sort_sort_direction = asort_direction;
	qsort((void *)displaytunes, displaycount, sizeof(void *), sort_sort_function);

	sort_arg = save_sort_arg;
	sort_sort_direction = save_sort_sort_direction;
}

void do_sort(void) {
	static int last_key_count = 0;

	if (same_key_twice_in_a_row(&last_key_count))
		sort_sort_direction *= -1;
	else
		sort_sort_direction = +1;

	qsort((void *)displaytunes, displaycount, sizeof(void *), sort_sort_function);
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

int find_duration_of_playlist(const char *playlistname) {
	FILE *f;
	char buf[1024];
	struct tune *tune;
	int duration;

	/* return 0 for now... */
	return 0;

	f = fopen(playlistname, "r");
	if (!f)
		return 0;

	duration = 0;
	while (fgets(buf, sizeof(buf), f)) {
		/*
		 * Ignore the "this-song-was-played-on-this-date" line
		 */
		if (is_all_digits(buf))
			continue;

		chop(buf);
		tune = find_tune_by_displayname(buf, 0);
		if (tune)
			duration += tune->ti->duration;
	}
	fclose(f);

	return duration;
}

void do_show_available_playlists(int all) {
	DIR *pdir;
	struct dirent *pde;
	char fullname[1024];
	struct stat statbuf;

	show_info(_("Generating list..."));

	pdir = opendir(playlist_dir);
	if (!pdir)
		return;

	clear_displaytunes();
	while (NULL != (pde = readdir(pdir))) {
		if (!strstr(pde->d_name, ".list"))
			continue;

		/*
		 * Don't show the autogenerated .list's (2003_12_14)
		 */
		if ((strlen(pde->d_name) == (4 + 2 + 2 + 4 + 1 + 1 + 1)) && isdigit(pde->d_name[0])
		    && !all)
			continue;

		if (!safe_path_join(fullname, sizeof(fullname), playlist_dir, pde->d_name))
			continue;

		memset(&statbuf, 0, sizeof(statbuf));
		stat(fullname, &statbuf);

		addtexttodisplay(pde->d_name, all ? 0 : find_duration_of_playlist(fullname),
		    statbuf.st_size, statbuf.st_mtime);
	}
	closedir(pdir);

	/*
	 * Sort playlist in alpha-order
	 */
	do_sort_work(ARG_NORMAL, 1);
	refresh_screen();
	show_info(_("Available play-lists."));
}

/* -------------------------------------------------------------------------- */

void do_view_artists(void) {
	char buf[1024];
	int i;
	char *s, *p;
	struct hashnode *wp;
	int cmd;
	int ch;
	int lo_ch;
	int hi_ch;

	show_info(_("Enter first letter (A..Z, 0..9) or SPACE for all. "));
	cmd = read_input_key();
	if (cmd != ' ' && !isalnum(cmd)) {
		action(cmd);
		return;
	}

	if (' ' == cmd) {
		lo_ch = '0';
		hi_ch = 'Z';
	} else {
		lo_ch = toupper(cmd);
		hi_ch = toupper(cmd);
	}

	hash_init();
	for (ch = lo_ch; ch <= hi_ch; ch++) {
		for (i = qsearch[ch].lo; i < qsearch[ch].hi; i++) {
			safe_strcpy(buf, alltunes[i].display, sizeof(buf));
			s = strstr(buf, " - ");
			if (s) {
				while (' ' == *s)
					*s-- = 0;
				p = strrchr(buf, '/');
				if (p)
					*p = 0;
				if (buf[0])
					hash_incword(buf, alltunes[i].ti->duration);
			}
		}
	}

	/*
	 * Put the unique words (artists),
	 * into the (unsorted) display list.
	 */
	clear_displaytunes();
	for (i = 0; i < NHASH; i++) {
		for (wp = hashnodebin[i]; wp; wp = wp->next)
			addtexttodisplay(wp->word, wp->duration, wp->count, 0);
	}
	hash_done();

	/*
	 * Sort the names of the bands in alpha-order.
	 */
	do_sort_work(ARG_NORMAL, 1);
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

/*
 * Scan all .list-files, count the number of times a song has been played,
 * display
 */

void do_view_toplist(void) {
	DIR *pdir;
	struct dirent *pde;
	char fullname[1024];
	int i;
	struct tune *findtune;
	struct tune *tune;
	struct hashnode *wp;
	FILE *f;
	char buf[1024];

	pdir = opendir(playlist_dir);
	if (!pdir) {
		show_info(_("Can't open playlist directory!"));
		return;
	}

	show_info(_("Generating list, step 1..."));

	hash_init();
	while (NULL != (pde = readdir(pdir))) {
		if (!isdigit(pde->d_name[0]) || !strstr(pde->d_name, ".list"))
			continue;

		if (!safe_path_join(fullname, sizeof(fullname), playlist_dir, pde->d_name))
			continue;

		f = fopen(fullname, "r");
		if (!f)
			continue;

		while (fgets(buf, sizeof(buf), f)) {
			/*
			 * Ignore the "this-song-was-played-on-this-date" line
			 */
			if (is_all_digits(buf))
				continue;

			chop(buf);
			hash_incword(buf, 0);
		}
		fclose(f);
	}
	closedir(pdir);

	show_info(_("Generating list, step 2..."));

	clear_displaytunes();
	for (i = 0; i < NHASH; i++) {
		for (wp = hashnodebin[i]; wp; wp = wp->next) {
			if (wp->count < 10)
				continue;

			findtune = find_tune_by_displayname(wp->word, 0);
			if (findtune && !tune_in_displaylist(findtune)) {
				tune = malloc(sizeof(struct tune));
				tune->display = findtune->display;
				tune->path = findtune->path;
				tune->search = findtune->search;
				tune->ti = malloc(sizeof(struct tuneinfo));
				tune->ti->filesize = wp->count;
				tune->ti->filedate = findtune->ti->filedate;
				tune->ti->duration = findtune->ti->duration;
				tune->ti->bitrate = findtune->ti->bitrate;
				tune->ti->genre = findtune->ti->genre;
				tune->ti->rating = findtune->ti->rating;
				addtunetodisplay(tune); /* TODO malloc/free */
			}
		}
	}
	hash_done();

	show_info(_("Generating list 3..."));

	/*
	 * Sort the names of the bands in reverse playcount-alpha-order.
	 */
	do_sort_work(ARG_SIZE, -1);
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

void do_view_available_genres(void) {
	int genre_count[256];
	int genre_duration[256];
	int i;

	memset(genre_count, 0, sizeof(genre_count));
	memset(genre_duration, 0, sizeof(genre_duration));
	for (i = 0; i < allcount; i++) {
		genre_count[alltunes[i].ti->genre]++;
		genre_duration[alltunes[i].ti->genre] += alltunes[i].ti->duration;
	}

	clear_displaytunes();
	for (i = 0; i < 256; i++) {
		if (genre_count[i] > 10) {
			addtexttodisplay(
			    genrename(i), genre_duration[i], genre_count[i], (i + 1) * -1);
		}
	}

	/*
	 * Sort in on song count in each genre in reverse
	 */
	do_sort_work(ARG_SIZE, -1);
	refresh_screen();
	show_info(_("Available genres."));
}

void do_show_one_genre(int genre) {
	int i;

	genre = abs(genre) - 1;

	clear_displaytunes();
	for (i = 0; i < allcount; i++) {
		if (genre == alltunes[i].ti->genre)
			addtunetodisplay(&alltunes[i]);
	}
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

/*
 * "-If  you want to generate a random integer
 *   between 1 and 10, you should always do it by
 *   using high-order bits, as in
 *        j=1+(int) (10.0*rand()/(RAND_MAX+1.0));
 *   and never by anything resembling
 *        j=1+(rand() % 10);
 *   (which uses lower-order bits)."
 */

time_t addweektotime(time_t t, int weeks) {
	return t + (86400 * (7 * weeks));
}

void do_show_new_songs(void) {
	char buf[1024];
	int i;
	char *s;
	struct hashnode *wp;
	int cmd;
	time_t newest;
	time_t lolimit;
	time_t hilimit;

	show_info(_("Enter weeks back (1..9) "));
	cmd = read_input_key();
	if (!isdigit(cmd)) {
		action(cmd);
		return;
	}

	newest = -1;
	for (i = 0; i < allcount; i++)
		if (alltunes[i].ti->filedate > newest)
			newest = alltunes[i].ti->filedate;
	lolimit = addweektotime(newest, -(cmd - '0'));
	hilimit = addweektotime(lolimit, 1);

	hash_init();
	for (i = 0; i < allcount; i++) {
		if (inrange(alltunes[i].ti->filedate, lolimit, hilimit)) {
			safe_strcpy(buf, alltunes[i].display, sizeof(buf));
			s = strchr(buf, '/');
			if (s)
				*s = 0;
			hash_incword(buf, alltunes[i].ti->duration);
		}
	}

	/*
	 * Put the unique words (artists),
	 * into the (unsorted) display list.
	 */
	clear_displaytunes();
	for (i = 0; i < NHASH; i++) {
		for (wp = hashnodebin[i]; wp; wp = wp->next)
			if (wp->count > 1)
				addtexttodisplay(wp->word, wp->duration, wp->count, 0);
	}
	hash_done();

	/*
	 * Sort the names of the bands in alpha-order.
	 */
	do_sort_work(ARG_NORMAL, 1);
	refresh_screen();
}

void do_view(void) {
	int cmd;
	int i;
	time_t newest;
	time_t lolimit;
	time_t hilimit;
	int randomnr;
	struct tm lotm;
	struct tm hitm;

	show_info(_("View: 0-9=Weeks back  R)andom  T)opList  A)rtists  G)enre  N)ew "));
	cmd = read_input_key();
	switch (cmd) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		if ('0' == cmd)
			lolimit = time(NULL) - 86400;
		else {
			newest = -1;
			for (i = 0; i < allcount; i++) {
				if (alltunes[i].ti->filedate > newest)
					newest = alltunes[i].ti->filedate;
			}
			lolimit = addweektotime(newest, -(cmd - '0'));
		}
		hilimit = addweektotime(lolimit, 1);

		clear_displaytunes();
		for (i = 0; i < allcount; i++) {
			if (inrange(alltunes[i].ti->filedate, lolimit, hilimit))
				addtunetodisplay(&alltunes[i]);
		}
		refresh_screen();

		localtime_r(&lolimit, &lotm);
		localtime_r(&hilimit, &hitm);
		show_info(_("Showing date context (%4d-%02d-%02d -- %4d-%02d-%02d)"),
		    1900 + lotm.tm_year, 1 + lotm.tm_mon, lotm.tm_mday, 1900 + hitm.tm_year,
		    1 + hitm.tm_mon, hitm.tm_mday);
		break;

	case 'R':
	case 'r':
		clear_displaytunes();
		for (i = 0; i < 100; i++) {
			randomnr = (int)((1.0 * allcount) * random() / (RAND_MAX + 1.0));
			addtunetodisplay(&alltunes[randomnr]);
		}
		refresh_screen();
		break;

	case 'G':
	case 'g':
		do_view_available_genres();
		break;

	case 'T':
	case 't':
		do_view_toplist();
		break;

	case 'A':
	case 'a':
		do_view_artists();
		break;

	case 'N':
	case 'n':
		do_show_new_songs();
		break;

	case 'd':
		clear_displaytunes();
		for (i = 0; i < allcount - 1; i++) {
			if (0 == strcmp(alltunes[i].search, alltunes[i + 1].search)) {
				addtunetodisplay(&alltunes[i]);
				addtunetodisplay(&alltunes[i + 1]);
				i++;
			}
		}
		refresh_screen();
		break;

	case 'D':
		clear_displaytunes();
		for (i = 0; i < allcount - 1; i++) {
			if (0 == strcmp(alltunes[i].search, alltunes[i + 1].search)
			    && (alltunes[i].ti->filesize == alltunes[i + 1].ti->filesize
				|| alltunes[i].ti->filedate == alltunes[i + 1].ti->filedate)) {
				addtunetodisplay(&alltunes[i]);
				addtunetodisplay(&alltunes[i + 1]);
				i++;
			}
		}
		refresh_screen();
		break;

	default:
		action(cmd);
	}
}

/* -------------------------------------------------------------------------- */

int is_infofile(const char *filename) {
	char *ext;

	ext = strrchr(filename, '.');
	if (ext) {
		if (0 == strcasecmp(ext, ".nfo"))
			return true;
		if (0 == strcasecmp(ext, ".txt"))
			return true;
		if (0 == strcasecmp(ext, ".doc"))
			return true;
	}

	return false;
}

void trim_trail(char *s) {
	int i;

	for (i = strlen(s) - 1; i >= 0 && isspace(s[i]); i--)
		s[i] = 0;
}

void do_show_infofiles(void) {
	DIR *pdir;
	FILE *f;
	struct dirent *pde;
	char fullname[1024];
	char workdir[1024];
	char text[255];
	char *p;
	bool hasinfo = false;
	int c;
	int colpos;
	bool lastempty = true;

	/*
	 * TODO: if no files found in the current directory.
	 *       try the parent directory.
	 */
	/*
	 * Find the path to the song we're playing now.
	 * Construct "/mp3/thepath/" from "/mp3/thepath/thesong.mp3"
	 */
	safe_strcpy(workdir, now_playing_tune->path, sizeof(workdir));
	p = strrchr(workdir, '/');
	if (p)
		*++p = 0;

	pdir = opendir(workdir);
	if (!pdir)
		return;

	while (NULL != (pde = readdir(pdir))) {
		if (!is_infofile(pde->d_name))
			continue;

		if (!safe_path_join(fullname, sizeof(fullname), workdir, pde->d_name))
			continue;

		f = fopen(fullname, "r");
		if (!f)
			continue;

		colpos = 0;
		while ((c = fgetc(f)) != EOF) {
			/*
			 * 1. Strip those silly ANSIGRAPH characters
			 * 2. Strip CR & LF characters.
			 *    File can be DOS- or *NIX-style...
			 */
			if (13 == c)
				continue;
			text[colpos++] = c > ' ' ? c : ' ';
			text[colpos] = 0;

			if (colpos > COLS - 2 || 10 == c) {
				trim_trail(text);

				/*
				 * Don't show two empty lines in a row
				 */
				if (!(lastempty && !strlen(text))) {
					if (!hasinfo) {
						clear_displaytunes();
						hasinfo = true;
					}
					addtexttodisplay(text, 0, 0, 0);
				}
				lastempty = !strlen(text);
				colpos = 0;
			}
		}

		if (hasinfo) {
			trim_trail(text);
			if (strlen(text))
				addtexttodisplay(text, 0, 0, 0);
		}
		fclose(f);
	}
	closedir(pdir);

	refresh_screen();
	if (!hasinfo)
		show_info(_("No information available."));
}

/* -------------------------------------------------------------------------- */

#define CONTEXT_LINES 20

void do_context(void) {
	static int last_key_count = 0;
	static int context_lines = CONTEXT_LINES;
	int i;
	int start;
	int stop;
	int cmd;
	char buf[1024];
	char *s;
	time_t lolimit;
	time_t hilimit;
	struct tm lotm;
	struct tm hitm;
	int r1;
	int r2;

	/*
	 * We must have a song playing to do any of these functions
	 */
	if (!now_playing_tune) {
		show_info(_("Nothing is playing!"));
		return;
	}

	/*  L=Length  S=Size  P=Path  "); */
	show_info(_("Context: F4=Songlist  G)enre  A)rtist  D)ate  I)nformation  R)andom "));
	cmd = read_input_key();
	switch (cmd) {
	case KEY_F(4):
		if (same_key_twice_in_a_row(&last_key_count))
			context_lines += CONTEXT_LINES;
		else
			context_lines = CONTEXT_LINES;

		start = 0;
		stop = 0;
		for (i = 0; i < allcount; i++) { /* TODO: Binary search*/
			if (&alltunes[i] == now_playing_tune) {
				start = i - context_lines;
				stop = i + context_lines;
				break;
			}
		}

		if (start < 0)
			start = 0;
		if (stop > allcount)
			stop = allcount;

		clear_displaytunes();
		for (i = start; i < stop; i++)
			addtunetodisplay(&alltunes[i]);

		for (i = start; i < stop; i++) {
			if (&alltunes[i] == now_playing_tune)
				break;
			user_move_cursor(+1, false);
		}
		refresh_screen();

		show_info(_("Showing songlist context."));
		break;

	case 'G':
	case 'g':
		clear_displaytunes();
		for (i = 0; i < allcount; i++) {
			if (now_playing_tune->ti->genre == alltunes[i].ti->genre)
				addtunetodisplay(&alltunes[i]);
		}
		refresh_screen();

		show_info(_("Showing genre context (%s)."), genrename(now_playing_tune->ti->genre));
		break;

	case 'A':
	case 'a':
		clear_displaytunes();

		safe_strcpy(buf, now_playing_tune->display, sizeof(buf));
		s = strtok(buf, "-");
		if (s) {
			only_searchables(s);
			for (i = 0; i < allcount; i++) {
				if (strstr(alltunes[i].search, s))
					addtunetodisplay(&alltunes[i]);
			}
		}

		refresh_screen();
		const char *artist_label = s ? s : _("(unknown)");
		show_info(_("Showing artist context (%s)."), artist_label);
		break;

	case 'D':
	case 'd':
		lolimit = addweektotime(now_playing_tune->ti->filedate, -1);
		hilimit = addweektotime(now_playing_tune->ti->filedate, +1);
		clear_displaytunes();
		for (i = 0; i < allcount; i++) {
			if (inrange(alltunes[i].ti->filedate, lolimit, hilimit))
				addtunetodisplay(&alltunes[i]);
		}
		refresh_screen();

		localtime_r(&lolimit, &lotm);
		localtime_r(&hilimit, &hitm);
		show_info(_("Showing date context (%4d-%02d-%02d -- %4d-%02d-%02d)"),
		    1900 + lotm.tm_year, 1 + lotm.tm_mon, lotm.tm_mday, 1900 + hitm.tm_year,
		    1 + hitm.tm_mon, hitm.tm_mday);
		break;

	case 'I':
	case 'i':
		do_show_infofiles();
		break;

	case 'R':
	case 'r':
		for (i = 0; i < displaycount; i++) {
			r1 = (int)((1.0 * displaycount) * random() / (RAND_MAX + 1.0));
			r2 = (int)((1.0 * displaycount) * random() / (RAND_MAX + 1.0));
			if (r1 != r2)
				swap(&displaytunes[r1], &displaytunes[r2]);
		}
		refresh_screen();
		break;

	default:
		action(cmd);
	}
}

/* -------------------------------------------------------------------------- */

/*
 * Linux makes this so easy with the help of symbolic links.
 *
 * Instead of copying 650MB+ for a full CD, we just create a bunch of
 * symbolic links pointing to the actual files to be burned.
 *
 * /burn is shared via SAMBA so our Windows client (Nero),
 * can burn the files as a MP3-CD or a real Audio-CD.
 */

void do_burn_playlist(void) {
	int i;
	char symname[1024];
	int burned = 0;
	int error;
	char *p;

	error = access("/burn", W_OK);
	if (error) {
		show_info(_("error: Can't write to directory '/burn'!"));
		return;
	}

	show_info(_("Preparing for burning..."));
	for (i = 0; i < playlistcount; i++) {
		snprintf(
		    symname, sizeof(symname), "/burn/%03d_%s.mp3", 1 + i, playlist[i]->display);

		/*
		 * !!2005-04-23 KB
		 * Start replacing /'s with '-' one step beyond the "/burn/" string.
		 */
		for (p = symname + 7; *p; p++) {
			if ('/' == *p)
				*p = '-';
		}

		if (can_open(playlist[i]->path)) {
			error = symlink(playlist[i]->path, symname);
			if (!error)
				burned++;
		}
		show_info(_("%d of %d songs prepared for burning."), burned, playlistcount);
	}
}

/* -------------------------------------------------------------------------- */

/*
 * We use the ISO definition of one kilobyte, 1 KiB = 1024 bytes
 */
#define ONE_KiB 1024

void do_time(void) {
	double total_size;
	double total_duration;
	int i;
	double s, m, h;
	int ss, mm, hh, days;
	time_t playends;
	char suffix[5];
	int base;

	total_size = 0.0;
	total_duration = 0.0;
	for (i = 0; i < displaycount; i++) {
		total_size += displaytunes[i]->ti->filesize;
		total_duration += displaytunes[i]->ti->duration;
	}

	base = 0;
	while (total_size > ONE_KiB) {
		total_size /= ONE_KiB;
		base += 3;
	}

	switch (base) {
	case 0:
		safe_strcpy(suffix, "", sizeof(suffix));
		break;
	case 3:
		safe_strcpy(suffix, "Ki", sizeof(suffix));
		break;
	case 6:
		safe_strcpy(suffix, "Mi", sizeof(suffix));
		break;
	case 9:
		safe_strcpy(suffix, "Gi", sizeof(suffix));
		break; /* Giga  2^30 = 1KSongs */
	case 12:
		safe_strcpy(suffix, "Ti", sizeof(suffix));
		break; /* Tera  2^40 = 1MSongs */
	case 15:
		safe_strcpy(suffix, "Pi", sizeof(suffix));
		break; /* Peta  2^50 = 1GSongs */
	case 18:
		safe_strcpy(suffix, "Ei", sizeof(suffix));
		break; /* Exa   2^60 = 1TSongs */
	case 21:
		safe_strcpy(suffix, "Zi", sizeof(suffix));
		break; /* Zetta 2^70 = 1PSongs */
	case 24:
		safe_strcpy(suffix, "Yi", sizeof(suffix));
		break; /* Yotta 2^80 = 1ESongs */
	default:
		safe_strcpy(suffix, "duh", sizeof(suffix));
		break; /* can't happen */
	}

	/*
	 * number of seconds
	 * Seconds to display
	 */
	s = total_duration;
	ss = (int)s % 60;

	/*
	 * number of minutes
	 * Minutes to display
	 */
	m = (s - ss) / 60;
	mm = (int)m % 60;

	/*
	 * number of hours
	 */
	h = (m - mm) / 60;
	hh = (int)h % 24;

	/*
	 * number of days
	 */
	days = (h - hh) / 24;

	playends = time(NULL) + total_duration;
	const char *time_str = ctime(&playends);
	if (!time_str)
		time_str = _("(unknown)");
	show_info(_("%d displayed songs, %0.1f %sBytes, %ddays %02d:%02d:%02d, %s"), displaycount,
	    total_size, suffix, days, hh, mm, ss, time_str);
}

/* -------------------------------------------------------------------------- */

/*
 * Show the songs in the playlist
 */

void do_show_playlist(void) {
	int i;

	clear_displaytunes();
	for (i = 0; i < playlistcount; i++)
		addtunetodisplay(playlist[i]);

	/*
	 * !!2004-01-25 KB
	 * If the current song is in the playlist,
	 * put the cursor at that song.
	 */
	if (tune_in_playlist(now_playing_tune)) {
		for (i = 0; i < playlistcount; i++) {
			if (now_playing_tune == playlist[i])
				break;
			user_move_cursor(+1, false);
		}
	}

	refresh_screen();
}

/* -------------------------------------------------------------------------- */

void do_search(void) {
	int matchfirstchar;
	int matchdirectory;
	int fuzzysearch;
	char *p;
	char lookfor[100];
	int i;
	int j;
	char **wordlist;
	int *negatelist;
	int words;
	int matches;
	char buf[1024];
	int onematch;

	matchfirstchar = isupper(search_string[0]);
	matchdirectory = '/' == search_string[0];
	fuzzysearch = '%' == search_string[0];

	/*
	 * !!2005-07-27 KB
	 * If this is a barcode (containing all digits),
	 * look for a playlist with the name 123456.list.
	 * If there is such a playlist, load it and start
	 * play the first song in that list.
	 */
	if (is_all_digits(search_string)) {
		do_load_playlist(search_string);
		if (playlistcount) {
			do_show_playlist();
			start_play(true, displaytunes[0]);
			search_string[0] = last_search_string[0] = 0;
			return;
		}
	}

	/*
	 * How many words is in the search string?
	 */
	words = 0;
	safe_strcpy(lookfor, search_string, sizeof(lookfor));
	for (p = strtok(lookfor, " "); p; p = strtok(NULL, " "))
		words++;

	/*
	 * Construct the wordlist array and each string
	 */
	int word_capacity = words;
	size_t word_bytes = (size_t)word_capacity;
	wordlist = (word_capacity > 0) ? malloc(word_bytes * sizeof(char *)) : NULL;
	negatelist = (word_capacity > 0) ? malloc(word_bytes * sizeof(int)) : NULL;
	if (word_capacity > 0 && (!wordlist || !negatelist)) {
		/* Out of memory - free what we allocated and return */
		free(wordlist);
		free(negatelist);
		return;
	}
	words = 0;
	safe_strcpy(lookfor, search_string, sizeof(lookfor));
	bool allocation_failed = false;
	for (p = strtok(lookfor, " "); p; p = strtok(NULL, " ")) {
		if (word_capacity == 0 || words >= word_capacity) {
			allocation_failed = true;
			break;
		}
		negatelist[words] = strchr(p, '!') ? 1 : 0;
		wordlist[words] = strdup(p);
		if (!wordlist[words]) {
			allocation_failed = true;
			break; /* Out of memory */
		}
		only_searchables(wordlist[words]);
		words++;
	}
	if (allocation_failed) {
		for (int k = 0; k < words; k++)
			free(wordlist[k]);
		free(negatelist);
		free(wordlist);
		return;
	}

	/*
	 * Search for it using SQLite!
	 */
	clear_displaytunes();

	if (fuzzysearch) {
		/* Fuzzy search - for now, just do a simple search */
		safe_strcpy(lookfor, search_string, sizeof(lookfor));
		only_searchables(lookfor);

		/* Use SQLite search */
		int count = 0;
		struct db_track **tracks = db_search_tracks(lookfor, &count);

		for (i = 0; i < count; i++) {
			struct db_track *db_track = tracks[i];

			/* Convert to tune structure and add to display */
			struct tune *tune = malloc(sizeof(struct tune));
			if (!tune) {
				db_free_track(db_track);
				continue;
			}

			tune->path = strdup(db_track->filepath);
			tune->display = strdup(db_track->display_name);
			tune->search = strdup(db_track->search_text);
			tune->ti = malloc(sizeof(struct tuneinfo));
			memcpy(tune->ti, &db_track->ti, sizeof(struct tuneinfo));

			addtunetodisplay(tune);
			db_free_track(db_track);
		}

		free(tracks);
	} else {
		/*
		 * !!2005-09-15 KB
		 * If the first letter is in uppercase, as in the search
		 * string "Cure", we use the qsearch array to do speedy
		 * searches among all artists that begin with "C".
		 * This trick reduces the number of calls to the
		 * strstr() and strcpy() functions, and most importantly
		 * it greatly reduces the accesses to the database.
		 */
		if (matchfirstchar) {
			/* For SQLite, we can use LIKE queries for first character matching */
			/* For now, just do a regular search */
		}

		/* Build search query for SQLite */
		char query[1024] = "";
		for (j = 0; j < words; j++) {
			if (j > 0) {
				if (negatelist[j])
					safe_strcat(query, " AND ", sizeof(query));
				else
					safe_strcat(query, " OR ", sizeof(query));
			} else {
				if (negatelist[j])
					safe_strcpy(query, " NOT ", sizeof(query));
			}

			if (matchdirectory) {
				safe_strcat(query, "filepath LIKE '%", sizeof(query));
				safe_strcat(query, wordlist[j], sizeof(query));
				safe_strcat(query, "%'", sizeof(query));
			} else if (matchfirstchar && j == 0) {
				/* First character match */
				char first_char[2] = { toupper(wordlist[j][0]), '\0' };
				safe_strcat(query, "(display_name LIKE '", sizeof(query));
				safe_strcat(query, first_char, sizeof(query));
				safe_strcat(query, "%' OR search_text LIKE '", sizeof(query));
				safe_strcat(query, first_char, sizeof(query));
				safe_strcat(query, "%')", sizeof(query));
			} else {
				/* Regular search */
				safe_strcat(query, "(display_name LIKE '%", sizeof(query));
				safe_strcat(query, wordlist[j], sizeof(query));
				safe_strcat(query, "%' OR search_text LIKE '%", sizeof(query));
				safe_strcat(query, wordlist[j], sizeof(query));
				safe_strcat(query, "%')", sizeof(query));
			}
		}

		/* Execute search query */
		int count = 0;
		struct db_track **tracks = db_search_tracks(search_string, &count);

		for (i = 0; i < count; i++) {
			struct db_track *db_track = tracks[i];

			/* Additional filtering for complex search logic */
			if (matchdirectory) {
				safe_strcpy(buf, db_track->filepath, sizeof(buf));
				only_searchables(buf);
				p = buf;
			} else {
				p = db_track->search_text;
			}

			matches = 0;
			for (j = 0; j < words; j++) {
				if (matchfirstchar && 0 == j) {
					/* First character match */
					onematch = (p[0] == toupper(wordlist[j][0]));
				} else {
					onematch = (strstr(p, wordlist[j]) != NULL);
				}
				if (negatelist[j])
					onematch = !onematch;
				matches += onematch;
			}

			if (matches == words) {
				/* Convert to tune structure and add to display */
				struct tune *tune = malloc(sizeof(struct tune));
				if (!tune) {
					db_free_track(db_track);
					continue;
				}

				tune->path = strdup(db_track->filepath);
				tune->display = strdup(db_track->display_name);
				tune->search = strdup(db_track->search_text);
				tune->ti = malloc(sizeof(struct tuneinfo));
				memcpy(tune->ti, &db_track->ti, sizeof(struct tuneinfo));

				addtunetodisplay(tune);
			}

			db_free_track(db_track);
		}

		free(tracks);
	}

	/*
	 * Free each string and the array
	 */
	for (i = 0; i < words; i++)
		free(wordlist[i]);
	free(negatelist);
	free(wordlist);
}

void do_enter(void) {
	/* Handle special quit commands */
	if (0 == strcmp(search_string, "zxcv") || 0 == strcmp(search_string, ":wq")) {
		wanna_quit = true;
		return;
	}

	/* If there are results, perform action on selected item */
	if (displaycount) {
		if (displaytunes[tunenr]->search) {
			/* Play the selected track */
			start_play(true, displaytunes[tunenr]);
		} else if (displaytunes[tunenr]->ti->filedate < 0) {
			/* Show genre listing */
			do_show_one_genre(displaytunes[tunenr]->ti->filedate);
		} else if (strstr(displaytunes[tunenr]->display, ".list")) {
			/* Load and show playlist */
			do_load_playlist(displaytunes[tunenr]->display);
			do_show_playlist();
		} else {
			/* Search for the selected item's name */
			safe_strcpy(
			    search_string, displaytunes[tunenr]->display, sizeof(search_string));
			only_searchables(search_string);
			trigger_realtime_search();
		}
	}
}

/* -------------------------------------------------------------------------- */

void do_move_song(int delta) {
	void *here;
	int newpos;

	int64_t candidate = (int64_t)tunenr + (int64_t)delta;
	if (candidate < 0 || candidate > INT_MAX)
		return;
	newpos = (int)candidate;
	if (newpos < 0 || newpos >= displaycount)
		return;

	here = displaytunes[tunenr];

	swap(&displaytunes[newpos], &displaytunes[tunenr]);

	/*
	 * Are we in the playlist?
	 */
	if (tunenr < playlistcount && (here == playlist[tunenr]))
		swap(&playlist[newpos], &playlist[tunenr]);

	refresh_screen();
}

/* -------------------------------------------------------------------------- */

/*
 * Show some info about the selected song
 */

void do_query_info(void) {
	static int last_key_count = 0;
	static bool show_path = false;
	struct tune *tune;
	struct tm tm;

	if (same_key_twice_in_a_row(&last_key_count))
		show_path = !show_path;
	else
		show_path = false;

	tune = displaytunes[tunenr];

	if (tune && tune->ti) {
		if (show_path) {
			show_info(tune->path);
		} else {
			localtime_r(&tune->ti->filedate, &tm);
			show_info(_("%4d-%02d-%02d %9d bytes, %02d:%02d minutes, %d kbps, '%s'"),
			    1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday, tune->ti->filesize,
			    tune->ti->duration / 60, tune->ti->duration % 60, tune->ti->bitrate,
			    genrename(tune->ti->genre));
		}
	}
}

/* -------------------------------------------------------------------------- */

/*
 * Let user select what is displayed to the left of each song
 */

void do_info(void) {
	int cmd;

	show_info(
	    _("Info: N)ormal  L)ength  S)ize  D)ate  B)itrate  G)enre  R)ating  P)ath  F)inish "));
	cmd = read_input_key();
	switch (cmd) {
	case ' ':
	case 'N':
	case 'n':
		sort_arg = ARG_NORMAL;
		break;
	case 'L':
	case 'l':
		sort_arg = ARG_LENGTH;
		break;
	case 'S':
	case 's':
		sort_arg = ARG_SIZE;
		break;
	case 'D':
	case 'd':
		sort_arg = ARG_DATE;
		break;
	case 'B':
	case 'b':
		sort_arg = ARG_BITRATE;
		break;
	case 'G':
	case 'g':
		sort_arg = ARG_GENRE;
		break;
	case 'R':
	case 'r':
		sort_arg = ARG_RATING;
		break;
	case 'P':
	case 'p':
		sort_arg = ARG_PATH;
		break;
#ifdef USE_FINISH
	case 'F':
	case 'f':
		sort_arg = ARG_FINISH;
		break;
#endif
	case KEY_F(1):
		sort_arg++;
		sort_arg %= ARG_MAXVAL;
		break;
	default:
		action(cmd);
		return;
	}

	show_info(displaytunes[tunenr]->path);
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

void do_save_displaystate(int state_num) {
	char statefilename[1024];
	FILE *f;
	int i;

	snprintf(statefilename, sizeof(statefilename), "%s%d.state", playlist_dir, state_num);
	f = fopen(statefilename, "w");
	if (f) {
		fprintf(f, "%s\n", last_search_string);
		fprintf(f, "%d\n", tunenr);
		fprintf(f, "%d\n", toptunenr);
		fclose(f);

		safe_strcat(statefilename, ".bin", sizeof(statefilename));
		f = fopen(statefilename, "w");
		if (f) {
			for (i = 0; i < displaycount; i++)
				fwrite(&displaytunes[i]->display, sizeof(char *), 1, f);
			fclose(f);
		}
	}
}

void do_restore_displaystate(int state_num) {
	char statefilename[1024];
	char buf[1024];
	FILE *f;
	int cnt = 0;
	char *address;

	snprintf(statefilename, sizeof(statefilename), "%s%d.state", playlist_dir, state_num);
	f = fopen(statefilename, "r");
	if (!f)
		return;

	clear_displaytunes_prim(false);
	while (fgets(buf, sizeof(buf), f)) {
		chop(buf);
		switch (cnt++) {
		case 0:
			safe_strcpy(search_string, buf, sizeof(search_string));
			safe_strcpy(last_search_string, buf, sizeof(last_search_string));
			break;
		case 1:
			tunenr = atoi(buf);
			break;
		case 2:
			toptunenr = atoi(buf);
			break;
		}
	}
	fclose(f);

	safe_strcat(statefilename, ".bin", sizeof(statefilename));
	f = fopen(statefilename, "r");
	if (f) {
		while (fread(&address, sizeof(char *), 1, f))
			addtunetodisplay(find_in_alltunes_by_display_pointer(address));
		fclose(f);
	}
}

void do_state(int cmd) {
	static int state_num = 0;

	show_info(_("State: S)ave  R)estore D=DEBUG"));
	if (!cmd)
		cmd = read_input_key();
	switch (cmd) {
	case 'S':
	case 's':
		do_save_displaystate(++state_num);
		show_info(_("State saved (%d)."), state_num);
		break;
	case 'R':
	case 'r':
		if (state_num)
			do_restore_displaystate(state_num--);
		show_info(_("State restored (%d)."), state_num);
		break;
	case 'D':
	case 'd':
		break;
	default:
		action(cmd);
		return;
	}
	refresh_screen();
}

/* -------------------------------------------------------------------------- */

/*
 * Handle search string input (backspace and character entry)
 */
static bool handle_search_input(int key, int *last_space_count) {
	int i;

	/* Explicitly reject arrow keys and navigation keys to prevent escape sequences
	 * from being interpreted as typeable characters when holding down keys */
	if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT
	    || key == KEY_HOME || key == KEY_END || key == KEY_PPAGE || key == KEY_NPAGE
	    || key == KEY_IC || key == KEY_DC || key == KEY_ENTER
	    || (key >= KEY_F(1) && key <= KEY_F(12))) {
		return false; /* Let these be handled by other key handlers */
	}

	/* Reject control characters and escape sequences (except typeable ones) */
	if (key < 32 && key != 13 && key != 27) {
		return false;
	}

	/* Handle backspace */
	if (KEY_BACKSPACE == key || 0x07f == key) {
		i = strlen(search_string);
		if (i)
			search_string[i - 1] = 0;
		update_searchstring();
		trigger_realtime_search();
		return true;
	}

	/* Handle typeable characters and special search chars */
	if ('!' == key || ':' == key || '%' == key || '/' == key || ' ' == key
	    || is_typeable_key(key)) {
		i = strlen(search_string);

		/* Validate special characters */
		switch (key) {
		case ':':
			/* Don't allow : at beginning or more than one : */
			if (0 == i || strchr(search_string, ':') != NULL)
				return true;
			break;

		case ' ':
			/* Double-tap spacebar advances to next song */
			if (same_key_twice_in_a_row(last_space_count)) {
				if (i)
					search_string[i - 1] = 0;
				update_searchstring();
				*last_space_count = 0;
				find_and_play_next_handler(0);
				return true;
			}
			/* Don't allow space at beginning or double spaces */
			if (0 == i || ' ' == search_string[i - 1])
				return true;
			break;

		case '/':
			/* Only one '/' at the beginning allowed */
			if (0 != i)
				return true;
			break;

		case '!':
			/* Don't allow double !'s */
			if ('!' == search_string[i - 1])
				return true;
			break;

		case '%':
			/* %'s must be in the beginning */
			if (0 != i)
				return true;
			break;
		}

		/* Add character to search string */
		if (i < 70) {
			search_string[i++] = key;
			search_string[i++] = 0;
			update_searchstring();
			trigger_realtime_search();
		}
		return true;
	}

	return false;
}

/*
 * Handle playlist input mode (save or cancel)
 */
static bool handle_input_mode(int key) {
	if (!in_input_mode)
		return false;

	switch (key) {
	case 13: /* Enter */
		save_playlist(search_string);
		show_info(_("Playlist '%s' saved."), search_string);
		in_input_mode = false;
		search_string[0] = last_search_string[0] = 0;
		update_searchstring();
		break;
	case 27: /* Escape */
		show_info(_("Save cancelled."));
		in_input_mode = false;
		search_string[0] = last_search_string[0] = 0;
		update_searchstring();
		break;
	}
	return true;
}

/*
 * Handle function keys (F1-F12)
 */
static bool handle_function_keys(int key) {
	switch (key) {
	case KEY_F(1):
		if (!displaycount)
			return true;
		do_info();
		return true;

	case KEY_F(2):
		do_view();
		return true;

	case KEY_F(3):
		do_sort();
		return true;

	case KEY_F(4):
		do_context();
		return true;

	case KEY_F(5):
		do_show_playlist();
		return true;

	case KEY_F(6):
		do_show_available_playlists(false);
		search_string[0] = last_search_string[0] = 0;
		update_searchstring();
		return true;

	case KEY_F(7):
		show_info(_("Enter a playlist name."));
		in_input_mode = true;
		safe_strcpy(search_string, latest_playlist_name, sizeof(search_string));
		update_searchstring();
		return true;

	case KEY_F(8):
		do_burn_playlist();
		return true;

	case KEY_F(9):
		do_time();
		return true;

	case KEY_F(10):
		return true;

	case KEY_F(11):
		load_all_songs();
		refresh_screen();
		return true;

	case KEY_F(12):
		do_show_available_playlists(true);
		search_string[0] = last_search_string[0] = 0;
		update_searchstring();
		return true;
	}
	return false;
}

/*
 * Handle navigation keys (arrows, Home, End, Page Up/Down)
 */
static bool handle_navigation_keys(int key) {
	switch (key) {
	case KEY_LEFT:
		col_step -= COLUMN_DELTA;
		if (col_step < 0)
			col_step = 0;
		refresh_screen();
		return true;

	case KEY_RIGHT:
		if (col_step < 150)
			col_step += COLUMN_DELTA;
		refresh_screen();
		return true;

	case KEY_HOME:
		tunenr = 0;
		toptunenr = 0;
		refresh_screen();
		return true;

	case KEY_END:
		tunenr = displaycount - 1;
		toptunenr = tunenr - middlesize + 1;
		if (toptunenr < 0)
			toptunenr = 0;
		refresh_screen();
		return true;

	case KEY_DOWN:
		if (!displaycount)
			return true;
		user_move_cursor(+1, true);
		wnoutrefresh(win_middle);
		return true;

	case KEY_UP:
		if (!displaycount)
			return true;
		user_move_cursor(-1, true);
		wnoutrefresh(win_middle);
		return true;

	case KEY_NPAGE:
		if (!displaycount)
			return true;
		user_move_cursor(+1 * (middlesize - 1), false);
		wnoutrefresh(win_middle);
		refresh_screen();
		return true;

	case KEY_PPAGE:
		if (!displaycount)
			return true;
		user_move_cursor(-1 * (middlesize - 1), false);
		wnoutrefresh(win_middle);
		refresh_screen();
		return true;
	}
	return false;
}

/*
 * Handle special commands (Ctrl keys, +, *, Tab, etc.)
 */
static bool handle_special_commands(int key) {
	switch (key) {
	case 27: /* Escape - clear search */
		search_string[0] = last_search_string[0] = 0;
		update_searchstring();
		return true;

	case 16: /* Ctrl-P - pause/unpause */
		if (player_pid) {
			signal(SIGCHLD, SIG_IGN);
			kill(player_pid, paused ? SIGCONT : SIGSTOP);
			if (paused)
				signal(SIGCHLD, find_and_play_next_handler);
			paused = !paused;
		}
		return true;

	case 21: /* Ctrl-U - move song up */
		if (!displaycount)
			return true;
		do_move_song(-1);
		action(KEY_UP);
		return true;

	case 4: /* Ctrl-D - move song down */
		if (!displaycount)
			return true;
		do_move_song(+1);
		action(KEY_DOWN);
		return true;

	case '+':
		if (!displaycount)
			return true;
		add_tune_to_playlist(displaytunes[tunenr]);
		action(KEY_DOWN);
		return true;

	case '*':
		if (!displaycount)
			return true;
		if (now_playing_tune)
			add_tune_to_playlist(now_playing_tune);
		add_tune_to_playlist(displaytunes[tunenr]);
		action(KEY_DOWN);
		return true;

	case 9: /* Tab - query info */
		if (!displaycount)
			return true;
		do_query_info();
		return true;

	case 12: /* Ctrl-L - redraw screen */
		wclear(win_top);
		wclear(win_info);
		wclear(win_middle);
		wclear(win_bottom);
		refresh_screen();
		return true;

	case KEY_RESIZE:
		endwin();
		make_ui();
		refresh_screen();
		wclear(win_top);
		wclear(win_info);
		wclear(win_middle);
		wclear(win_bottom);
		refresh_screen();
		doupdate();
		return true;

	case 13: /* Enter */
	case '>':
		do_enter();
		return true;
	}
	return false;
}

/*
 * Normalize raw input so incomplete escape sequences (e.g. split arrow keys)
 * are converted back into their intended KEY_* values before UI dispatch.
 */
static int translate_escape_sequence(const int *seq, size_t len) {
	if (len == 0)
		return 0;

	int prefix = seq[0];

	if ('[' == prefix) {
		if (len == 2) {
			switch (seq[1]) {
			case 'A':
				return KEY_UP;
			case 'B':
				return KEY_DOWN;
			case 'C':
				return KEY_RIGHT;
			case 'D':
				return KEY_LEFT;
			case 'F':
				return KEY_END;
			case 'H':
				return KEY_HOME;
			default:
				break;
			}
		} else if (len >= 3 && seq[len - 1] == '~') {
			int value = 0;
			for (size_t i = 1; i + 1 < len; i++) {
				if (!isdigit((unsigned char)seq[i]))
					return 0;
				value = (value * 10) + (seq[i] - '0');
			}

			switch (value) {
			case 1:
			case 7:
				return KEY_HOME;
			case 2:
				return KEY_IC;
			case 3:
				return KEY_DC;
			case 4:
			case 8:
				return KEY_END;
			case 5:
				return KEY_PPAGE;
			case 6:
				return KEY_NPAGE;
			case 11:
				return KEY_F(1);
			case 12:
				return KEY_F(2);
			case 13:
				return KEY_F(3);
			case 14:
				return KEY_F(4);
			case 15:
				return KEY_F(5);
			case 17:
				return KEY_F(6);
			case 18:
				return KEY_F(7);
			case 19:
				return KEY_F(8);
			case 20:
				return KEY_F(9);
			case 21:
				return KEY_F(10);
			case 23:
				return KEY_F(11);
			case 24:
				return KEY_F(12);
			default:
				break;
			}
		}
	} else if ('O' == prefix && len == 2) {
		switch (seq[1]) {
		case 'A':
			return KEY_UP;
		case 'B':
			return KEY_DOWN;
		case 'C':
			return KEY_RIGHT;
		case 'D':
			return KEY_LEFT;
		case 'F':
			return KEY_END;
		case 'H':
			return KEY_HOME;
		case 'P':
			return KEY_F(1);
		case 'Q':
			return KEY_F(2);
		case 'R':
			return KEY_F(3);
		case 'S':
			return KEY_F(4);
		default:
			break;
		}
	}

	return 0;
}

static int read_input_key(void) {
	static int pending_seq[8];
	static size_t pending_len = 0;
	static size_t pending_index = 0;

	for (;;) {
		if (pending_index < pending_len) {
			int queued = pending_seq[pending_index++];
			if (pending_index >= pending_len) {
				pending_index = 0;
				pending_len = 0;
			}
			return queued;
		}

		int key = wgetch(win_top);
		if (key == ERR)
			continue;

		if (key != 27)
			return key;

		int seq[8];
		size_t seq_len = 0;

		nodelay(win_top, true);
		for (;;) {
			int ch = wgetch(win_top);
			if (ch == ERR)
				break;
			if (seq_len < (sizeof(seq) / sizeof(seq[0])))
				seq[seq_len++] = ch;
			if ((ch >= 'A' && ch <= 'Z') || ch == '~')
				break;
		}
		nodelay(win_top, false);

		if (!seq_len)
			return 27;

		int translated = translate_escape_sequence(seq, seq_len);
		if (translated)
			return translated;

		pending_len = (seq_len < (sizeof(pending_seq) / sizeof(pending_seq[0])))
		    ? seq_len
		    : (sizeof(pending_seq) / sizeof(pending_seq[0]));
		for (size_t i = 0; i < pending_len; i++)
			pending_seq[i] = seq[i];
		pending_index = 0;
		return 27;
	}
}

/*
 * Main keyboard action handler
 * Dispatches to specialized handlers based on key type
 */
void action(int key) {
	static int last_space_count = 0;

	/* Handle search string input */
	if (handle_search_input(key, &last_space_count))
		return;

	/* Handle playlist input mode */
	if (handle_input_mode(key))
		return;

	/* Handle function keys */
	if (handle_function_keys(key))
		return;

	/* Handle navigation keys */
	if (handle_navigation_keys(key))
		return;

	/* Handle special commands */
	if (handle_special_commands(key))
		return;
}

/* -------------------------------------------------------------------------- */

void print_version(void) {
	printf(
	    "GLACIERA - Heavy Duty Jukebox - %s - %s\n", complete_version(), __DATE__ " " __TIME__);
	printf("Copyright (c) Plux Stahre 2025\n");
	printf("Portions Copyright (c) Kristian Wiklund 1997 <kw@dtek.chalmers.se>\n");
}

int main(int argc, char **argv) {
	int arg;

	/*
	 * CRITICAL: Set locale before any curses initialization
	 * This enables UTF-8 support in ncurses
	 */
	setlocale(LC_ALL, "");

#ifdef USE_GETTEXT
	/* setup internationalization of message-strings via gettext(): */
	bindtextdomain("glaciera", "./locale");
	textdomain("glaciera");
#endif

	/* Define long options */
	static struct option long_options[]
	    = { { "help", no_argument, 0, 'h' }, { "version", no_argument, 0, 'v' },
		      { "theme-preview", no_argument, 0, 't' }, { 0, 0, 0, 0 } };

	int option_index = 0;
	while ((arg = getopt_long(argc, argv, "hvr", long_options, &option_index)) > -1) {
		switch (arg) {
		case 'r':
			opt_read_ahead = 0;
			break;
		case 'h':
		case '?':
			print_version();
			printf("usage: glaciera [-h] [-v] [-r] [--theme-preview]\n");
			printf("options:\n");
			printf("  -h, --help          Show this help message\n");
			printf("  -v, --version       Show version information\n");
			printf("  -r                  *Don't* use read ahead\n");
			printf("  --theme-preview     Display all available themes and "
			       "exit\n");
			exit(0);
			break;
		case 'v':
			print_version();
			exit(0);
		case 't':
			/* Initialize config to get themes directory */
			if (!config_init()) {
				fprintf(stderr, "Failed to initialize configuration\n");
				exit(EXIT_FAILURE);
			}
			/* Display theme previews */
			char themes_path[1024];
			snprintf(themes_path, sizeof(themes_path), "%s/themes", xdg_config_dir);
			display_theme_previews(themes_path);
			exit(0);
		}
	}

	/*
	 * Detached threads have all resources freed when they terminate.
	 * Joinable threads have state information about the thread
	 * kept even after they finish.
	 */
	pthread_attr_init(&detachedattr);
	pthread_attr_setdetachstate(&detachedattr, PTHREAD_CREATE_DETACHED);

	print_version();

	/* Initialize new XDG-compliant configuration system */
	if (!config_init()) {
		fprintf(stderr, "Failed to initialize configuration\n");
		exit(EXIT_FAILURE);
	}

	/* Validate that player binaries exist */
	if (!config_validate_players()) {
		fprintf(stderr, "Error: One or more player binaries are missing.\n");
		fprintf(stderr, "Please install them or update your configuration.\n");
		exit(EXIT_FAILURE);
	}

	music_register_all_modules();

	/*
	 * Save the name of the users home direcory for later use.
	 * !!2004-01-22 KB
	 * Use a subdirectory in users home to avoid cluttering the
	 * base directory with playlist files.
	 * Playlists are now stored in /home/joeuser/playlists
	 * The directory is created with 700 permission
	 */
	const char *home = config_get_home_dir();
	if (!safe_path_join(playlist_dir, sizeof(playlist_dir), home, "playlists/")) {
		fprintf(stderr, "Error: playlist path too long\n");
		exit(EXIT_FAILURE);
	}
	mkdir(playlist_dir, 0700);

	/*
	 * Initialize SQLite database (using XDG_DATA_HOME)
	 */
	if (!db_init(config_get_db_path())) {
		printf(_("Failed to initialize database!\n"));
		exit(0);
	}

	build_fastarrays();
	load_all_songs();
	if (!allcount) {
		printf(_("No songs in song database!\n"));
		exit(0);
	}

	/*
	 * make random() generate random numbers...
	 */
	srandom(time(NULL));

	/*
	 * Build initial display list which contains *no* songs...
	 */
	clear_displaytunes_prim(false);

	make_ui();
	refresh_screen();

	signal(SIGALRM, update_song_progress_handler);
	update_song_progress_handler(0);
	do {
		in_action = true;
		after_move();
		doupdate();
		in_action = false;
		arg = read_input_key();
		key_count++;
		in_action = true;
		action(arg);
		in_action = false;
	} while (!wanna_quit);

	if (player_pid)
		stop_playing(player_pid);

	endwin();
	exit(0);
}
