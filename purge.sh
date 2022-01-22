#!/bin/bash

# This script removes all stored comments older than 15 days
#
# You might want to use this to purge old comments in order to avoid your disk
# space growing endlessly. Just put it in your crontab to be called once per
# day.

wd=$(dirname $0)
cd $wd/
wd=$(pwd)

rm `find ./data/ -type f -mtime +15`
