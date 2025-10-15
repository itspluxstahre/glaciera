#!/bin/bash

        echo -n 'char* svn_version(void) { static char* SVN_Version = "' \
                                       > svn_version.c
        svnversion -n .                   >> svn_version.c
        echo '"; return SVN_Version; }'   >> svn_version.c
