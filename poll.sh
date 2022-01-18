#!/bin/bash

nthreads=16

items_old=""
while true ; do
    items=$(curl -s https://hacker-news.firebaseio.com/v0/updates.json | jq -c '.items[]' - )
    if [ "$items" = "$items_old" ] ; then
        echo "No updates.. sleeping"
        sleep 5
        continue
    fi

    cmds=""
    items_old="$items"

    for i in $items ; do
        cmds="a=\$(curl -s https://hacker-news.firebaseio.com/v0/item/$i.json) && if [ \"\$(echo \$a | jq -r .type)\" == \"comment\" ] ; then id=\$(echo \$a | jq -r .id) ; pid=\$(echo \$a | jq -r .parent) ; b=\$(curl -s https://hacker-news.firebaseio.com/v0/item/\$pid.json) ; by=\$(echo \$b | jq -r .by) ; mkdir -p data/\$by ; echo \$a > data/\$by/\$id ; fi
$cmds"
    done

    date
    time echo "$cmds" | xargs --max-procs=$nthreads --replace /bin/bash -c "{}"

    sleep 1
done
