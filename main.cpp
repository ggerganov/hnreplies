//
// C++ implementation of the HNReplies service.
//
// The service accumulates all comments that occur on Hacker News in real-time
// and categorizes them in a directory structure that is convenient for making
// queries about the replies to a certain user. The directory structure is as
// follows:
//
// ./data/
//        username0/
//                  29977271
//                  29977272
//                  29977279
//        username1/
//                  29977276
//                  29977283
//        some_one/
//                  29977276
//                  29977283
//        .../
//
// The file "./data/$username/$id" contains the JSON obtained from the HN API query:
//
//   https://hacker-news.firebaseio.com/v0/item/$id.json
//
// Sample result:
//
//   $ cat data/mayoff/2921983
//     {
//         "by" : "norvig",
//         "id" : 2921983,
//         "kids" : [ 2922097, 2922429, 2924562, 2922709, 2922573, 2922140, 2922141 ],
//         "parent" : 2921506,
//         "text" : "Aw shucks, guys ... you make me blush with your compliments.<p>Tell you what, Ill make a deal: I'll keep writing if you keep reading. K?",
//         "time" : 1314211127,
//         "type" : "comment"
//     }
//
// The above example means that item 2921983 is a comment written by user
// "norvig" in reply to a comment or a story by user "mayoff" with id "2921506".
//
// The main advantage of the data organized in this way is that the items are
// grouped by "username", so it is very easy to query for all replies to a
// certain user.
//
// More info: https://github.com/ggerganov/hnreplies
//

#include "json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <curl/curl.h>

#include <map>
#include <array>
#include <deque>
#include <string>
#include <chrono>
#include <vector>
#include <thread>
#include <fstream>
#include <filesystem>

#define MAX_PARALLEL 64

//#define DEBUG_SIGPIPE

void sigpipe_handler([[maybe_unused]] int signal) {
#ifdef DEBUG_SIGPIPE
    std::ofstream fout("SIGPIPE.OCCURED");
    fout << signal << std::endl;
    fout.close();
#endif
}

//
// curl functionality
//
namespace {

struct Data {
    CURL *eh = NULL;
    bool running = false;
    std::string uri = "";
    std::string content = "";
};

CURLM *g_cm;

int g_nFetches = 0;
uint64_t g_totalBytesDownloaded = 0;
std::deque<std::string> g_fetchQueue;
std::map<std::string, std::string> g_fetchCache;
std::array<Data, MAX_PARALLEL> g_fetchData;

uint64_t t_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); // duh ..
}

size_t writeFunction(void *ptr, size_t size, size_t nmemb, Data* data) {
    size_t bytesDownloaded = size*nmemb;
    g_totalBytesDownloaded += bytesDownloaded;

    data->content.append((char*) ptr, bytesDownloaded);

    g_fetchCache[data->uri] = std::move(data->content);
    data->content.clear();

    return bytesDownloaded;
}

void addTransfer(CURLM *cm, int idx, std::string && uri) {
    if (g_fetchData[idx].eh == NULL) {
        g_fetchData[idx].eh = curl_easy_init();
    }

    CURL *eh = g_fetchData[idx].eh;
    curl_easy_setopt(eh, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(eh, CURLOPT_PRIVATE, &g_fetchData[idx]);
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, writeFunction);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, &g_fetchData[idx]);
    curl_multi_add_handle(cm, eh);
}

bool curlInit() {
    struct sigaction sh;
    struct sigaction osh;

    sh.sa_handler = &sigpipe_handler; //Can set to SIG_IGN
    // Restart interrupted system calls
    sh.sa_flags = SA_RESTART;

    // Block every signal during the handler
    sigemptyset(&sh.sa_mask);

    if (sigaction(SIGPIPE, &sh, &osh) < 0) {
        return false;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    g_cm = curl_multi_init();

    curl_multi_setopt(g_cm, CURLMOPT_MAXCONNECTS, (long)MAX_PARALLEL);

    return true;
}

void curlFree() {
    curl_multi_cleanup(g_cm);
    curl_global_cleanup();
}

std::string getJSONForURI_impl(const std::string & uri) {
    if (auto it = g_fetchCache.find(uri); it != g_fetchCache.end()) {
        auto res = std::move(g_fetchCache[uri]);
        g_fetchCache.erase(it);

        return res;
    }

    return "";
}

uint64_t getTotalBytesDownloaded() {
    return g_totalBytesDownloaded;
}

uint64_t getNFetches() {
    return g_nFetches;
}

void requestJSONForURI_impl(std::string uri) {
    g_fetchQueue.push_back(std::move(uri));
}

void updateRequests_impl() {
    CURLMsg *msg;
    int msgs_left = -1;

    while ((msg = curl_multi_info_read(g_cm, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            Data* data;
            CURL *e = msg->easy_handle;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data);
            data->running = false;
            curl_multi_remove_handle(g_cm, e);
            //curl_easy_cleanup(e);
            //data->eh = NULL;
        } else {
            fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
        }
    }

    int still_alive = 1;

    curl_multi_perform(g_cm, &still_alive);

    while (still_alive < MAX_PARALLEL && g_fetchQueue.size() > 0) {
        int idx = 0;
        while (g_fetchData[idx].running) {
            ++idx;
            if (idx == g_fetchData.size()) break;
        }
        if (idx == g_fetchData.size()) break;

        auto uri = std::move(g_fetchQueue.front());
        g_fetchQueue.pop_front();

        ++g_nFetches;

        g_fetchData[idx].running = true;
        g_fetchData[idx].uri = uri;
        addTransfer(g_cm, idx, std::move(uri));

        ++still_alive;
    }
}

}

namespace HN {

using URI      = std::string;
using ItemId   = int;
using ItemIds  = std::vector<ItemId>;
using ItemData = std::map<std::string, std::string>;

const URI kAPIItem    = "https://hacker-news.firebaseio.com/v0/item/";
const URI kAPIUpdates = "https://hacker-news.firebaseio.com/v0/updates.json";

enum class ItemType : int {
    Unknown,
    Story,
    Comment,
    Job,
    Poll,
    PollOpt,
};

struct Comment {
    std::string by;
    ItemId id = 0;
    ItemIds kids;
    ItemId parent = 0;
    std::string text;
    uint64_t time = 0;
};

void requestJSONForURI(std::string uri) {
    requestJSONForURI_impl(std::move(uri));
}

std::string getJSONForURI(const std::string & uri, int nRetries, int tRetry_ms) {
    auto json = getJSONForURI_impl(uri);

    // retry until the query has been processed or we run out of retries
    while (json.empty() && nRetries-- > 0) {
        updateRequests_impl();
        json = getJSONForURI_impl(uri);
        std::this_thread::sleep_for(std::chrono::milliseconds(tRetry_ms));
    }

    return json;
}

URI getItemURI(ItemId id) {
    return kAPIItem + std::to_string(id) + ".json";
}

ItemIds getChangedItemsIds() {
    auto data = JSON::parseJSONMap(getJSONForURI(HN::kAPIUpdates, 0, 0));
    return JSON::parseIntArray(data["items"]);
}

ItemType getItemType(const ItemData & itemData) {
    if (itemData.find("type") == itemData.end()) return ItemType::Unknown;

    auto strType = itemData.at("type");

    if (strType == "story") return ItemType::Story;
    if (strType == "comment") return ItemType::Comment;
    if (strType == "job") return ItemType::Job;
    if (strType == "poll") return ItemType::Poll;
    if (strType == "pollopt") return ItemType::PollOpt;

    return ItemType::Unknown;
}

void parseComment(const ItemData & data, Comment & res) {
    try {
        res.by = data.at("by");
    } catch (...) {
        res.by = "[deleted]";
    }
    try {
        res.id = std::stoi(data.at("id"));
    } catch (...) {
        res.id = 0;
    }
    try {
        res.kids = JSON::parseIntArray(data.at("kids"));
    } catch (...) {
        res.kids.clear();
    }
    try {
        res.parent = std::stoi(data.at("parent"));
    } catch (...) {
        res.parent = 0;
    }
    try {
        //res.text = parseHTML(data.at("text"));
        res.text = data.at("text");
    } catch (...) {
        res.text = "";
    }
    try {
        res.time = std::stoll(data.at("time"));
    } catch (...) {
        res.time = 0;
    }
}

bool same(const ItemIds & ids0, const ItemIds & ids1) {
    if (ids0.size() != ids1.size()) return false;

    int n = ids0.size();
    for (int i = 0; i < n; ++i) {
        if (ids0[i] != ids1[i]) return false;
    }

    return true;
}

}

int main() {
    curlInit();

    HN::ItemIds idsOld;
    HN::ItemIds idsCur;

    printf("[I] Connecting to the HN API ..\n");

    while (true) {
        // query the HN API about which items have been updated
        // the API seems to provide updates every 30 seconds
        HN::requestJSONForURI(HN::kAPIUpdates);
        do {
            updateRequests_impl();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            idsCur = HN::getChangedItemsIds();
        } while (idsCur.empty());

        if (HN::same(idsCur, idsOld)) {
            printf("[I] No new comments since last update -- sleeping ..\n");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        idsOld = idsCur;

        printf("[I] %d items have been updated\n", (int) idsCur.size());

        const auto tStart = ::t_ms();

        // enque queries to the HN API about the new items
        for (const auto & id : idsCur) {
            HN::requestJSONForURI(HN::getItemURI(id));
        }

        // process MAX_PARALLEL of the queries
        updateRequests_impl();

        HN::ItemIds parents;
        std::map<HN::ItemId, std::string> by;
        std::map<HN::ItemId, std::string> raw;
        std::map<HN::ItemId, HN::Comment> comments;

        int nComments = 0;
        int nOther = 0;
        int nUpdated = 0;
        int nUnknown = 0;
        int nErrors = 0;

        for (const auto & id : idsCur) {
            // have we already processed this item?
            if (by.find(id) != by.end()) continue;

            auto & json = raw[id];
            json = HN::getJSONForURI(HN::getItemURI(id), 10, 1000);

            if (json.empty()) {
                ++nErrors;
                fprintf(stderr, "[E] Failed to get update for item %d\n", id);
                continue;
            }

            const auto data = JSON::parseJSONMap(json);
            const auto type = HN::getItemType(data);

            try {
                by[id] = data.at("by");
            } catch (...) {
                ++nErrors;
                fprintf(stderr, "[E] Failed to parse 'by' for item %d\n", id);
                continue;
            }

            // we are only interested in replies, so we check if the item's type is "comment"
            switch (type) {
                case HN::ItemType::Comment:
                    {
                        ++nComments;
                        auto & cur = comments[id];
                        parseComment(data, cur);

                        // we are interested who is this comment in reply to
                        // so we enque a query about the parent for later:
                        parents.push_back(cur.parent);
                        HN::requestJSONForURI(HN::getItemURI(cur.parent));
                    }
                    break;
                default:
                    {
                        ++nOther;
                    }
                    break;
            };
        }

        // iterate all parent items that have been observed in the updated comments
        for (const auto & id : parents) {
            // we have already processed this item?
            if (by.find(id) != by.end()) continue;

            const auto json = HN::getJSONForURI(HN::getItemURI(id), 10, 1000);

            if (json.empty()) {
                ++nErrors;
                fprintf(stderr, "[E] Failed to get update for item %d\n", id);
                continue;
            }

            const auto data = JSON::parseJSONMap(json);
            try {
                by[id] = data.at("by");
            } catch (...) {
                ++nErrors;
                fprintf(stderr, "[E] Failed to parse 'by' for item %d\n", id);
                continue;
            }
        }

        // otput the raw JSON of the updated items in the folders of the corresponding parent
        for (const auto & [ id, cur ] : comments) {
            if (by.find(cur.parent) == by.end()) {
                ++nUnknown;
                fprintf(stderr, "[E] Parent %d of item %d is unknown\n", cur.parent, id);
                continue;
            }

            ++nUpdated;

            const std::string pathDir = "./data/" + by[cur.parent];
            std::filesystem::create_directories(pathDir);

            const std::string pathReply = pathDir + "/" + std::to_string(id);
            std::ofstream fout(pathReply);
            fout << raw.at(id) << std::endl;
            fout.close();
        }

        const auto tElapsed = ::t_ms() - tStart;
        printf("[I] Time: %6lu ms  Comments: %3d  Updated: %3d  Unknown: %3d  Errors: %3d  Other: %3d | Total requests: %7d (%lu bytes)\n",
               tElapsed, nComments, nUpdated, nUnknown, nErrors, nOther, g_nFetches, g_totalBytesDownloaded);

        if (tElapsed > 30000) {
            fprintf(stderr, "[W] Update took more than 30 seconds - some data might have been missed\n");
        }
    }

    curlFree();

    return 0;
}
