/*
 * 2006-01-15 KB
 * 	Converted to FastCGI, to avoid calling load_all_songs() for every request.
 * 
 * TODO: 
 * 	The big timesaver, though, is that we can build some cool, 
 * 	not-yet-implemented search algos
 */
#include <fcgi_stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <ctype.h>
#include "common.h"

static int allcount = 0;
#ifdef DEBUG
static int calledcount = 0;
#endif
static unsigned int mm[5];
static BITS *showthese = NULL;

static void load_all_songs(void)
{
        int i;
        int f;
        struct stat ss;
        char buf[1024];

        for (i = 0; i < 5; i++) {
		switch (i) {
		case 0:
		case 2:
		case 3:
	                snprintf(buf, sizeof(buf), "%s%d.db", opt_mp3path, i);
	                f = open(buf, O_RDONLY);
	                fstat(f, &ss);
	                if (i == 0) 
	                        allcount = ss.st_size / sizeof(struct tune);
	                mm[i] = (unsigned int) mmap(0, ss.st_size, PROT_READ, MAP_SHARED, f, 0);
	                close(f);
			break;
		}
        }

	showthese = bitalloc(allcount);
}

static int do_search(char *search_string)
{
        int matchfirstchar;
        char *p;
        char lookfor[100];
        int i;
        int j;
        char ** wordlist;
        int words;
        int matches;
        int onematch;
        struct tune0 *base;
	int total_matches = 0;

        matchfirstchar = isupper(search_string[0]);

        /*
         * How many words is in the search string?
         */
        words = 0;
        strcpy(lookfor, search_string);
        for (p = strtok(lookfor, " "); p; p = strtok(NULL, " "))
                words++;

        /*
         * Construct the wordlist array and each string
         */
        wordlist = malloc(words * sizeof(char *));
        words = 0;
        strcpy(lookfor, search_string);
        for (p = strtok(lookfor, " "); p; p = strtok(NULL, " ")) {
                wordlist[words] = strdup(p);
                only_searchables(wordlist[words]);
                words++;
        }

        /*
         * Search for it!
         */
        base = (void *) mm[0];
        for (i = 0; i < allcount; i++, base++) {
                p = (char *) ((unsigned int)base->p3 + mm[3]);  /*search*/
                matches = 0;
                for (j = 0; j < words; j++) {
                        if (matchfirstchar && j == 0)
                                onematch = strstr(p, wordlist[j]) == p;
                        else
                                onematch = strstr(p, wordlist[j]) != NULL;
                        matches += onematch;
                }

                if (matches == words) {
			bitset(showthese, i); 
			total_matches++;
		}
        }

        /*
         * Free each string and the array
         */
        for (i = 0; i < words; i++)
                free(wordlist[i]);
        free(wordlist);
	
	return total_matches;
}

void show_matches(void)
{	
        struct tune0 *base;
	int i;
        char *p;

        base = (void *) mm[0];
        for (i = 0; i < allcount; i++, base++) {
		if (bittest(showthese, i)) {
			p = (char *) ((unsigned int)base->p2 + mm[2]);  /*display*/
			puts(p);
		}
        }
}

/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	char *q;
	char buf[80] = "";

#if 0
	read_rc_file(); 
#endif	
	sanitize_rc_parameters(FALSE);
	
        build_fastarrays();
        load_all_songs();
	
	while (FCGI_Accept() >= 0) {
		printf("Content-type: text/html\n\n");
		printf("<html>");
		printf("<head>");
		printf("<title>");
		printf("MP3BERG");
		printf("</title>");
		printf("</head>");
		printf("<body>");

		bitnull(showthese, allcount);
		
		buf[0] = 0;
		q = getenv("QUERY_STRING");
		if (q && 'q' == *q) {
			q++;
			if ('=' == *q) {
				q++;
				strncpy(buf, q, sizeof(buf));
				
				/*
				 * Sanitize users input.
				 * Allow only A-Z and 0-9. 
				 * That's it. Simple and easy.
				 * "+" gets translated to " ".
				 */
				sanitize_user_input(buf);
			}
		}
	
		printf("MP3BERG: Searching %d songs<br>", allcount);
#ifdef DEBUG
		printf("called %d times", calledcount++);
#endif		
	
		printf("<form method=get action=/cgi-bin/searchmp3berg.fcgi>");
		printf("<input type=input name=q value=\"%s\" size=30>", buf);
		printf("<input type=submit value=\"Search song\">");
		printf("</form>");
	
		if (buf[0]) {
			printf("Search result for: <b>%s</b>, %d matches<br>", buf, do_search(buf));
			printf("<hr>");
			printf("<pre>");
			show_matches();        
			printf("</pre>");
		}
	
		printf("</body>");
		printf("</html>");
	}

        exit(0);
}
