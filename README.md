# HNReplies

A service for getting the latest replies to an HN user

Sample API endpoint: https://hnreplies.ggerganov.com/get.php

```bash
#
# Get latest replies to "dang"
#

$ curl "https://hnreplies.ggerganov.com/get.php?u=dang&print=pretty"
{
    "replies": [
        {
            "by": "dncornholio",
            "id": 30008834,
            "parent": 30008561,
            "text": "Agreed! Next time I will gather more arguments.",
            "time": 1642686636,
            "type": "comment"
        },
        {
            "by": "MaxBarraclough",
            "id": 30008321,
            "parent": 30008042,
            "text": "Got it, thanks.",
            "time": 1642683577,
            "type": "comment"
        },
        ...
    ]
}
```

## Motivation

Currently the [Hacker News forum](https://news.ycombinator.com/) lacks the functionality to notify its users when another user has responded to their comment.
In some cases this can cause a user to miss a reply in a discussion in which they had participated recently. Some users consider this to be a feature rather
than a deficiency. In any case, this is a simple attempt to offer such functionality in the form of a 3rd party service.

The HNReplies service tracks all comments that happen on HN in realtime by querying the official [HN API](https://github.com/HackerNews/API) and grouping the
comments by their parent user. The collected data can then be used to efficiently query for the latest replies to a certain user.

The service is very basic and can be easily self-hosted on a low-end machine. It depends only on [curl](https://curl.se).

Although I implemented this mostly for educational purposes, maybe I it can potentially find some useful applications. For example, you can write a script
that sends an e-mail notification every time someone responds to you on HN. All it takes is to query the HNReplies service with your username every ~5
minutes and check for new replies.

## How it works?

The straightforward implementation is 20-30 lines of Bash script ([./poll.sh](./poll.sh)):

- We poll the HN API every 30 seconds to get all items that have been updated recently
- We then iterate the items and look for comments (i.e. Stories, Polls, etc. are ignored)
- For every comment, we query the HN API about its parent item in order to get the username that they are replying to
- We store the updated item in a folder named as the parent username

This process produces the following directory structure:

    data/
    ├── asciimov/
    │   └── 29015505
    ├── AshamedCaptain/
    │   ├── 30010021
    │   └── 30011553
    ├── bobcostas55/
    │   └── 30007698
    ├── CobrastanJorji/
    │   └── 30012581
    ├── dack/
    │   └── 30013307
    ├── daveaiello/
    │   ├── 30012555
    │   ├── 30013087
    │   ├── 30013242
    │   └── 30013325
    ├── dberhane/
    │   └── 30007270
    ├── deadcoder0904/
    │   └── 29996499
    ...

Each leaf is a text file with the JSON result of querying the HN API about the corresponding item Id.
For example, the file `./data/mayoff/2921983` contains the JSON obtained from the HN API query:

  [https://hacker-news.firebaseio.com/v0/item/2921983.json](https://hacker-news.firebaseio.com/v0/item/2921983.json?print=pretty)

Sample result:

    $ cat data/mayoff/2921983
    {
        "by" : "norvig",
        "id" : 2921983,
        "kids" : [ 2922097, 2922429, 2924562, 2922709, 2922573, 2922140, 2922141 ],
        "parent" : 2921506,
        "text" : "Aw shucks, guys ... you make me blush with your compliments.<p>Tell you what, Ill make a deal: I'll keep writing if you keep reading. K?",
        "time" : 1314211127,
        "type" : "comment"
    }

Running the service continuously will accumulate new HN data and organize it in the described way.

The initial Bash implementation turned out to be a bit inefficient for a low-end machine, so I also added a C++ implementation that performs much better.
The source code is in the [./main.cpp](./main.cpp) file and it essentially does the same thing as the Bash script using `libcurl`.

To run it:

```bash
git clone https://github.com/ggerganov/hnreplies
cd hnreplies
make
./poll
```

The service performs about ~200 requests / minute to the HN API. The actual value depends on the activity on HN.

## How to use?

Sample usages are given in the [./public](./public) folder of this repo.

---

### [./public/get.php](./public/get.php)

This is a simple PHP endpoint that uses the accumulated data by the HNReplies service to return the latest replies to a user.
I've hosted an example on https://hnreplies.ggerganov.com/ and you can use it as follows:

```
#
# Get latest replies to "dang"
#

$ curl "https://hnreplies.ggerganov.com/get.php?u=dang&print=pretty"
```
Or directly in the browser:

https://hnreplies.ggerganov.com/get.php?u=dang&print=pretty

The returned replies are orderer chronologically. You can modify the PHP script to provide some other data that you might want.

---

### [./public/index.html](./public/index.html)

A basic web page that sends an XHR request to the PHP endpoint and displays the results in HTML.
For example, checkout the following URL:

https://hnreplies.ggerganov.com/index.html?u=dang

---
