#!/bin/sh

make clean
scan-build -analyze-headers --experimental-checks -k -V -stats -maxloop 50 make
