#!/bin/bash

# This script lets you run individual input files to test specific cases,
# but it must be run on the production server in order to use the full tag DB.

# Need to verify that we are on sgdata
local_machine=`hostname -f`
#if [ $local_machine != "sgdata.motus.org" ]; then
#    echo "This script is only to be run on the production server: sgdata.motus.org"
#    exit 1
#fi

# Next, get any command line options
while getopts ":htd" opt; do
  case ${opt} in
    h ) # process option a
      ;;
    d )
      rm tagdb.sqlite
      sqlite3 tagdb.sqlite <tagDB.sql
      ;;
    t ) # process option t
      ;;
    \? ) echo "Usage: cmd [-h] [-t]"
      ;;
  esac
done
