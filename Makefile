#
# Makefile for mp3berg
#
# 2005-09-14 KB
#	Yes, we compile with "pentium" as target, so we can
# 	use old throw-away boxes (P100's) as clients.
#
# 2005-09-16 KB
#	Create the backup archive as bzip2
#
# 2006-01-15 KB
#	Changes for FastCGI-enabled searchmp3berg.fcgi
#
# 2007-03-18 KB
# 	Debianizations !
#

# NetBSD:
#CC = gcc -O2 -Wall -pedantic -I/usr/pkg/include -L/usr/pkg/lib  -Wl,-R/usr/pkg/lib

# Linux:
CC = clang -O3 -Wall -pedantic -I/opt/homebrew/include -L/opt/homebrew/lib

# OpenBSD:
#CC = gcc -O2 -Wall -pedantic -I/usr/local/include -L/usr/local/lib 

all: mp3berg mp3build searchmp3berg.fcgi

clean:
	rm -f *.o
	rm -f mp3berg
	rm -f mp3build
	rm -f searchmp3berg.fcgi
	rm -f callgrind.out.*
	rm -f *~
	rm -f core.*

dist: mp3build mp3berg 
	tar cjf dist/mp3berg-`date +%Y-%m-%d`.dist.i386.tar.bz mp3build mp3berg README rippers mp3bergrc

backup: clean
	tar cjf ../`date +%Y-%m-%d`src.tar.bz2 *

mp3build: mp3build.o common.o common.h svn_version.o mod_mp3.o mod_ogg.o mod_flac.o mod_pls.o music.o
	$(CC) -o mp3build mp3build.o common.o svn_version.o music.o mod_mp3.o mod_ogg.o mod_flac.o mod_pls.o -Wall -lpthread -lvorbisfile -lvorbis -lFLAC -logg -lm
#	strip mp3build

mp3berg: mp3berg.o common.o common.h svn_version.o mod_mp3.o mod_ogg.o mod_flac.o mod_pls.o music.o
	$(CC) -o mp3berg mp3berg.o common.o svn_version.o mod_mp3.o mod_ogg.o mod_flac.o mod_pls.o music.o -Wall -lncurses -lpthread -lvorbisfile -lvorbis -lFLAC -logg -lm
	strip mp3berg

searchmp3berg.fcgi: searchmp3berg.o common.o common.h svn_version.o
	$(CC) -o searchmp3berg.fcgi searchmp3berg.o common.o -lfcgi 
	strip searchmp3berg.fcgi

install:
	install mp3berg $(DESTDIR)/usr/bin
	install mp3build $(DESTDIR)/usr/bin

##
## on every build, record the working copy revision string
##
svn_version.o:
	echo -n 'char* svn_version(void) { static char* SVN_Version = "' \
	                               > svn_version.c
	svnversion -n .                   >> svn_version.c
	echo '"; return SVN_Version; }'   >> svn_version.c
	gcc -c svn_version.c

debs: clean
	dpkg-buildpackage -rfakeroot -uc -b
