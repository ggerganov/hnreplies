#!/bin/bash

#
# BASH implementation of the HNReplies service.
#
# The service accumulates all comments that occur on Hacker News in real-time
# and categorizes them in a directory structure that is convenient for making
# queries about the replies to a certain user. The directory structure is as
# follows:
#
# ./data/
#        username0/
#                  29977271
#                  29977272
#                  29977279
#        username1/
#                  29977276
#                  29977283
#        some_one/
#                  29977276
#                  29977283
#        .../
#
# The file "./data/$username/$id" contains the JSON obtained from the HN API query:
#
#   https://hacker-news.firebaseio.com/v0/item/$id.json
#
# Sample result:
#
#   $ cat data/mayoff/2921983
#     {
#         "by" : "norvig",
#         "id" : 2921983,
#         "kids" : [ 2922097, 2922429, 2924562, 2922709, 2922573, 2922140, 2922141 ],
#         "parent" : 2921506,
#         "text" : "Aw shucks, guys ... you make me blush with your compliments.<p>Tell you what, Ill make a deal: I'll keep writing if you keep reading. K?",
#         "time" : 1314211127,
#         "type" : "comment"
#     }
#
# The above example means that item 2921983 is a comment written by user
# "norvig" in reply to a comment or a story by user "mayoff" with id "2921506".
#
# The main advantage of organizing the data in this way is that the items are
# grouped by "username", so it is trivial to query for all replies to a certain
# user.
#
# This service uses the curl and jq command line tools to process new comments.
# Even though the curl requests are ran in parallel, this script can be quite
# slow and CPU intensive.
#
# The "main.cpp" program contains a more efficient C++ implementation of the
# same service.
#
# More info: https://github.com/ggerganov/hnreplies
#

# How many threads to use to run the curl commands
nthreads=16

items_old=""
while true ; do
    # Query the HN API about which items (i.e. comments, stories, polls, etc.) have changed lately
    items=$(curl -s https://hacker-news.firebaseio.com/v0/updates.json | jq -c '.items[]' - )
    if [ "$items" = "$items_old" ] ; then
        echo "No updates.. sleeping"
        sleep 5
        continue
    fi

    cmds=""
    items_old="$items"

    # Construct a set of curl commands that will be executed in parallel via xargs
    for i in $items ; do
        # For each updated item:
        # - check if the "type" field equals "comment"
        # - if yes: parse the item id and the "parent" id - pid
        # - query the HN API about the "parent" item and parse the "by" field
        # - the "by" field of the parent indicates to who the reply is
        # - store the RAW JSON of the current item in the folder "./data/$by/"
        #
        cmds="a=\$(curl -s https://hacker-news.firebaseio.com/v0/item/$i.json) && if [ \"\$(echo \$a | jq -r .type)\" == \"comment\" ] ; then id=\$(echo \$a | jq -r .id) ; pid=\$(echo \$a | jq -r .parent) ; b=\$(curl -s https://hacker-news.firebaseio.com/v0/item/\$pid.json) ; by=\$(echo \$b | jq -r .by) ; mkdir -p data/\$by ; echo \$a > data/\$by/\$id ; fi
$cmds"
    done

    date

    # run the commands
    time echo "$cmds" | xargs --max-procs=$nthreads --replace /bin/bash -c "{}"

    sleep 1
done
