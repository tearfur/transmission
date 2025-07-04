// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_ANNOUNCER_MODULE
#error only the libtransmission announcer module should #include this header.
#endif

#include <array>
#include <chrono>
#include <cstdint> // uint64_t
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/announcer.h"
#include "libtransmission/interned-string.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-mgr.h" // tr_pex
#include "libtransmission/tr-macros.h" // tr_peer_id_t
#include "libtransmission/utils.h"

struct tr_url_parsed_t;

void tr_tracker_http_scrape(tr_session const* session, tr_scrape_request const& req, tr_scrape_response_func on_response);

void tr_tracker_http_announce(tr_session const* session, tr_announce_request const& req, tr_announce_response_func on_response);

void tr_announcerParseHttpAnnounceResponse(tr_announce_response& response, std::string_view benc, std::string_view log_name);

void tr_announcerParseHttpScrapeResponse(tr_scrape_response& response, std::string_view benc, std::string_view log_name);

[[nodiscard]] constexpr std::string_view tr_announce_event_get_string(tr_announce_event e)
{
    switch (e)
    {
    case TR_ANNOUNCE_EVENT_COMPLETED:
        return "completed";

    case TR_ANNOUNCE_EVENT_STARTED:
        return "started";

    case TR_ANNOUNCE_EVENT_STOPPED:
        return "stopped";

    default:
        return "";
    }
}

struct tr_announce_request
{
    tr_announce_event event = {};
    bool partial_seed = false;

    /* the port we listen for incoming peers on */
    tr_port port;

    // see discussion of tr_announce_key_t that type's declaration
    tr_announce_key_t key;

    /* the number of peers we'd like to get back in the response */
    int64_t numwant = 0;

    /* the number of bytes we uploaded since the last 'started' event */
    uint64_t up = 0;

    /* the number of good bytes we downloaded since the last 'started' event */
    uint64_t down = 0;

    /* the number of bad bytes we downloaded since the last 'started' event */
    uint64_t corrupt = 0;

    /* the total size of the torrent minus the number of bytes completed */
    uint64_t leftUntilComplete = 0;

    /* the tracker's announce URL */
    tr_interned_string announce_url;

    /* key generated by and returned from an http tracker.
     * see tr_announce_response.tracker_id_str */
    std::string tracker_id;

    /* the torrent's peer id.
     * this changes when a torrent is stopped -> restarted. */
    tr_peer_id_t peer_id;

    /* the torrent's info_hash */
    tr_sha1_digest_t info_hash;

    /* the name to use when deep logging is enabled */
    std::string log_name;
};

struct tr_announce_response
{
    /* the torrent's info hash */
    tr_sha1_digest_t info_hash = {};

    /* whether or not we managed to connect to the tracker */
    bool did_connect = false;

    /* whether or not the scrape timed out */
    bool did_timeout = false;

    /* preferred interval between announces.
     * transmission treats this as the interval for periodic announces */
    time_t interval = 0;

    /* minimum interval between announces. (optional)
     * transmission treats this as the min interval for manual announces */
    time_t min_interval = 0;

    /* how many peers are seeding this torrent */
    std::optional<int64_t> seeders;

    /* how many peers are downloading this torrent */
    std::optional<int64_t> leechers;

    /* how many times this torrent has been downloaded */
    std::optional<int64_t> downloads;

    /* IPv4 peers that we acquired from the tracker */
    std::vector<tr_pex> pex;

    /* IPv6 peers that we acquired from the tracker */
    std::vector<tr_pex> pex6;

    /* human-readable error string on failure, or nullptr */
    std::string errmsg;

    /* human-readable warning string or nullptr */
    std::string warning;

    /* key generated by and returned from an http tracker.
     * if this is provided, subsequent http announces must include this. */
    std::string tracker_id;

    /* tracker extension that returns the client's public IP address.
     * https://www.bittorrent.org/beps/bep_0024.html */
    std::optional<tr_address> external_ip;

    static constexpr struct
    {
        static constexpr int compare(tr_announce_response const& lhs, tr_announce_response const& rhs)
        {
            if (auto val = tr_compare_3way(lhs.did_connect, rhs.did_connect); val != 0)
            {
                return val;
            }

            if (auto val = tr_compare_3way(lhs.did_timeout, rhs.did_timeout); val != 0)
            {
                return -val;
            }

            // Non-empty error message most likely means we reached the tracker
            return -tr_compare_3way(std::empty(lhs.errmsg), std::empty(rhs.errmsg));
        }

        constexpr bool operator()(tr_announce_response const& lhs, tr_announce_response const& rhs) const noexcept
        {
            return compare(lhs, rhs) > 0;
        }
    } CompareFailed{};
};

// --- SCRAPE

/* pick a number small enough for common tracker software:
 *  - ocelot has no upper bound
 *  - opentracker has an upper bound of 64
 *  - udp protocol has an upper bound of 74
 *  - xbtt has no upper bound
 *
 * This is only an upper bound: if the tracker complains about
 * length, announcer will incrementally lower the batch size.
 */
auto inline constexpr TrMultiscrapeMax = 60U;

auto inline constexpr TrAnnounceTimeoutSec = std::chrono::seconds{ 45 };
auto inline constexpr TrScrapeTimeoutSec = std::chrono::seconds{ 30 };

struct tr_scrape_request
{
    /* the scrape URL */
    tr_interned_string scrape_url;

    /* the name to use when deep logging is enabled */
    std::string log_name;

    /* info hashes of the torrents to scrape */
    std::array<tr_sha1_digest_t, TrMultiscrapeMax> info_hash;

    /* how many hashes to use in the info_hash field */
    size_t info_hash_count = 0U;
};

struct tr_scrape_response_row
{
    /* the torrent's info_hash */
    tr_sha1_digest_t info_hash;

    /* how many peers are seeding this torrent */
    std::optional<int64_t> seeders;

    /* how many peers are downloading this torrent */
    std::optional<int64_t> leechers;

    /* how many times this torrent has been downloaded */
    std::optional<int64_t> downloads;

    /* the number of active downloaders in the swarm.
     * this is a BEP 21 extension that some trackers won't support.
     * http://www.bittorrent.org/beps/bep_0021.html#tracker-scrapes  */
    std::optional<int64_t> downloaders;
};

struct tr_scrape_response
{
    /* whether or not we managed to connect to the tracker */
    bool did_connect = false;

    /* whether or not the scrape timed out */
    bool did_timeout = false;

    /* how many info hashes are in the 'rows' field */
    size_t row_count;

    /* the individual torrents' scrape results */
    std::array<tr_scrape_response_row, TrMultiscrapeMax> rows;

    /* the raw scrape url */
    tr_interned_string scrape_url;

    /* human-readable error string on failure, or nullptr */
    std::string errmsg;

    /* minimum interval (in seconds) allowed between scrapes.
     * this is an unofficial extension that some trackers won't support. */
    time_t min_request_interval;
};
