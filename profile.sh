#!/bin/sh
rm /mp3/*.db
./mp3build -f
gprof mp3build  |less


