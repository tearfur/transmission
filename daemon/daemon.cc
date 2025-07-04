// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio> /* printf */
#include <iostream>
#include <iterator> /* std::back_inserter */
#include <memory>
#include <string_view>
#include <vector>

#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif

#ifdef _WIN32
#include <process.h> /* getpid */
#else
#include <sys/time.h> /* timeval */
#include <unistd.h> /* getpid */
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/timer-ev.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/watchdir.h>

#include "daemon.h"

struct tr_ctor;
struct tr_session;
struct tr_torrent;

#ifdef WITH_SYSTEMD

#include <cinttypes>
#include <ctime>

#include <systemd/sd-daemon.h>

#else

// no-op
#define sd_notify(status, str)
#define sd_notifyf(status, fmt, ...)

#endif

using namespace std::literals;
using libtransmission::Watchdir;

namespace
{
char constexpr MyName[] = "transmission-daemon";
char constexpr Usage[] = "Transmission " LONG_VERSION_STRING
                         "  https://transmissionbt.com/\n"
                         "A fast and easy BitTorrent client\n"
                         "\n"
                         "transmission-daemon is a headless Transmission session that can be\n"
                         "controlled via transmission-qt, transmission-remote, or its web interface.\n"
                         "\n"
                         "Usage: transmission-daemon [options]";

// --- Config File

auto constexpr Options = std::array<tr_option, 47>{
    { { 'a', "allowed", "Allowed IP addresses. (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", true, "<list>" },
      { 'b', "blocklist", "Enable peer blocklists", "b", false, nullptr },
      { 'B', "no-blocklist", "Disable peer blocklists", "B", false, nullptr },
      { 'c', "watch-dir", "Where to watch for new torrent files", "c", true, "<directory>" },
      { 'C', "no-watch-dir", "Disable the watch-dir", "C", false, nullptr },
      { 941, "incomplete-dir", "Where to store new torrents until they're complete", nullptr, true, "<directory>" },
      { 942, "no-incomplete-dir", "Don't store incomplete torrents in a different location", nullptr, false, nullptr },
      { 'd', "dump-settings", "Dump the settings and exit", "d", false, nullptr },
      { 943, "default-trackers", "Trackers for public torrents to use automatically", nullptr, true, "<list>" },
      { 'e', "logfile", "Dump the log messages to this filename", "e", true, "<filename>" },
      { 'f', "foreground", "Run in the foreground instead of daemonizing", "f", false, nullptr },
      { 'g', "config-dir", "Where to look for configuration files", "g", true, "<path>" },
      { 'p', "port", "RPC port (Default: " TR_DEFAULT_RPC_PORT_STR ")", "p", true, "<port>" },
      { 't', "auth", "Require authentication", "t", false, nullptr },
      { 'T', "no-auth", "Don't require authentication", "T", false, nullptr },
      { 'u', "username", "Set username for authentication", "u", true, "<username>" },
      { 'v', "password", "Set password for authentication", "v", true, "<password>" },
      { 'V', "version", "Show version number and exit", "V", false, nullptr },
      { 810, "log-level", "Must be 'critical', 'error', 'warn', 'info', 'debug', or 'trace'.", nullptr, true, "<level>" },
      { 811, "log-error", "Deprecated. Use --log-level=error", nullptr, false, nullptr },
      { 812, "log-info", "Deprecated. Use --log-level=info", nullptr, false, nullptr },
      { 813, "log-debug", "Deprecated. Use --log-level=debug", nullptr, false, nullptr },
      { 'w', "download-dir", "Where to save downloaded data", "w", true, "<path>" },
      { 800, "paused", "Pause all torrents on startup", nullptr, false, nullptr },
      { 'o', "dht", "Enable distributed hash tables (DHT)", "o", false, nullptr },
      { 'O', "no-dht", "Disable distributed hash tables (DHT)", "O", false, nullptr },
      { 'y', "lpd", "Enable local peer discovery (LPD)", "y", false, nullptr },
      { 'Y', "no-lpd", "Disable local peer discovery (LPD)", "Y", false, nullptr },
      { 830, "utp", "Enable µTP for peer connections", nullptr, false, nullptr },
      { 831, "no-utp", "Disable µTP for peer connections", nullptr, false, nullptr },
      { 'P', "peerport", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "P", true, "<port>" },
      { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", false, nullptr },
      { 'M', "no-portmap", "Disable portmapping", "M", false, nullptr },
      { 'L',
        "peerlimit-global",
        "Maximum overall number of peers (Default: " TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ")",
        "L",
        true,
        "<limit>" },
      { 'l',
        "peerlimit-torrent",
        "Maximum number of peers per torrent (Default: " TR_DEFAULT_PEER_LIMIT_TORRENT_STR ")",
        "l",
        true,
        "<limit>" },
      { 910, "encryption-required", "Encrypt all peer connections", "er", false, nullptr },
      { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", false, nullptr },
      { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", false, nullptr },
      { 'i', "bind-address-ipv4", "Where to listen for peer connections", "i", true, "<ipv4 addr>" },
      { 'I', "bind-address-ipv6", "Where to listen for peer connections", "I", true, "<ipv6 addr>" },
      { 'r', "rpc-bind-address", "Where to listen for RPC connections", "r", true, "<ip addr>" },
      { 953,
        "global-seedratio",
        "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio",
        "gsr",
        true,
        "ratio" },
      { 954,
        "no-global-seedratio",
        "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio",
        "GSR",
        false,
        nullptr },
      { 994, "sequential-download", "Enable sequential download by default", "seq", false, nullptr },
      { 995, "no-sequential-download", "Disable sequential download by default", "SEQ", false, nullptr },
      { 'x', "pid-file", "Enable PID file", "x", true, "<pid-file>" },
      { 0, nullptr, nullptr, nullptr, false, nullptr } }
};
static_assert(Options[std::size(Options) - 2].val != 0);

[[nodiscard]] std::string getConfigDir(int argc, char const* const* argv)
{
    int c;
    char const* optstr;
    int const ind = tr_optind;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optstr)) != TR_OPT_DONE)
    {
        if (c == 'g')
        {
            return optstr;
        }
    }

    tr_optind = ind;

    return tr_getDefaultConfigDir(MyName);
}

auto onFileAdded(tr_session* session, std::string_view dirname, std::string_view basename)
{
    auto const lowercase = tr_strlower(basename);
    auto const is_torrent = tr_strv_ends_with(lowercase, ".torrent"sv);
    auto const is_magnet = tr_strv_ends_with(lowercase, ".magnet"sv);

    if (!is_torrent && !is_magnet)
    {
        return Watchdir::Action::Done;
    }

    auto const filename = tr_pathbuf{ dirname, '/', basename };
    tr_ctor* const ctor = tr_ctorNew(session);

    bool retry = false;

    if (is_torrent)
    {
        if (!tr_ctorSetMetainfoFromFile(ctor, filename, nullptr))
        {
            retry = true;
        }
    }
    else // is_magnet
    {
        auto content = std::vector<char>{};
        auto error = tr_error{};
        if (!tr_file_read(filename, content, &error))
        {
            tr_logAddWarn(
                fmt::format(
                    fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                    fmt::arg("path", basename),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
            retry = true;
        }
        else
        {
            content.push_back('\0'); // zero-terminated string
            auto const* data = std::data(content);
            if (!tr_ctorSetMetainfoFromMagnetLink(ctor, data, nullptr))
            {
                retry = true;
            }
        }
    }

    if (retry)
    {
        tr_ctorFree(ctor);
        return Watchdir::Action::Retry;
    }

    if (tr_torrentNew(ctor, nullptr) == nullptr)
    {
        tr_logAddError(fmt::format(fmt::runtime(_("Couldn't add torrent file '{path}'")), fmt::arg("path", basename)));
    }
    else
    {
        bool trash = false;
        bool const test = tr_ctorGetDeleteSource(ctor, &trash);

        if (test && trash)
        {
            tr_logAddInfo(fmt::format(fmt::runtime(_("Removing torrent file '{path}'")), fmt::arg("path", basename)));

            if (auto error = tr_error{}; !tr_sys_path_remove(filename, &error))
            {
                tr_logAddError(
                    fmt::format(
                        fmt::runtime(_("Couldn't remove '{path}': {error} ({error_code})")),
                        fmt::arg("path", basename),
                        fmt::arg("error", error.message()),
                        fmt::arg("error_code", error.code())));
            }
        }
        else
        {
            tr_sys_path_rename(filename, tr_pathbuf{ filename, ".added"sv });
        }
    }

    tr_ctorFree(ctor);
    return Watchdir::Action::Done;
}

[[nodiscard]] constexpr char const* levelName(tr_log_level level)
{
    switch (level)
    {
    case TR_LOG_CRITICAL:
        return "CRT";
    case TR_LOG_ERROR:
        return "ERR";
    case TR_LOG_WARN:
        return "WRN";
    case TR_LOG_DEBUG:
        return "dbg";
    case TR_LOG_TRACE:
        return "trc";
    default:
        return "inf";
    }
}

void printMessage(
    FILE* ostream,
    std::chrono::system_clock::time_point now,
    tr_log_level level,
    std::string_view name,
    std::string_view message,
    std::string_view filename,
    long line)
{
    auto out = tr_strbuf<char, 2048U>{};

    if (std::empty(name))
    {
        fmt::format_to(std::back_inserter(out), "{:s} ({:s}:{:d})", message, filename, line);
    }
    else
    {
        fmt::format_to(std::back_inserter(out), "{:s} {:s} ({:s}:{:d})", name, message, filename, line);
    }

    if (ostream != nullptr)
    {
        auto buf = std::array<char, 64>{};
        auto const timestr = tr_logGetTimeStr(now, std::data(buf), std::size(buf));
        fmt::print(ostream, "[{:s}] {:s} {:s}\n", timestr, levelName(level), out.c_str());
    }

#ifdef HAVE_SYSLOG

    else /* daemon... write to syslog */
    {
        int priority;

        /* figure out the syslog priority */
        switch (level)
        {
        case TR_LOG_CRITICAL:
            priority = LOG_CRIT;
            break;

        case TR_LOG_ERROR:
            priority = LOG_ERR;
            break;

        case TR_LOG_WARN:
            priority = LOG_WARNING;
            break;

        case TR_LOG_INFO:
            priority = LOG_INFO;
            break;

        default:
            priority = LOG_DEBUG;
            break;
        }

        syslog(priority, "%s", out.c_str());
    }

#endif
}

void printMessage(
    FILE* ostream,
    tr_log_level level,
    std::string_view name,
    std::string_view message,
    std::string_view filename,
    long line)
{
    printMessage(ostream, std::chrono::system_clock::now(), level, name, message, filename, line);
}

void pumpLogMessages(FILE* log_stream)
{
    tr_log_message* list = tr_logGetQueue();

    for (tr_log_message const* l = list; l != nullptr; l = l->next)
    {
        printMessage(log_stream, l->when, l->level, l->name, l->message, l->file, l->line);
    }

    // two reasons to not flush stderr:
    // 1. it's usually redundant, since stderr flushes itself
    // 2. when running as a systemd unit, it's redirected to a socket
    if (log_stream != stderr)
    {
        fflush(log_stream);
    }

    tr_logFreeQueue(list);
}

void periodic_update(evutil_socket_t /*fd*/, short /*what*/, void* arg)
{
    static_cast<tr_daemon*>(arg)->periodic_update();
}

tr_rpc_callback_status on_rpc_callback(tr_session* /*session*/, tr_rpc_callback_type type, tr_torrent* /*tor*/, void* arg)
{
    if (type == TR_RPC_SESSION_CLOSE)
    {
        static_cast<tr_daemon*>(arg)->stop();
    }
    return TR_RPC_OK;
}

tr_variant load_settings(char const* config_dir)
{
    auto app_defaults_map = tr_variant::Map{ 6U };
    app_defaults_map.try_emplace(TR_KEY_watch_dir, tr_variant::unmanaged_string(""sv));
    app_defaults_map.try_emplace(TR_KEY_watch_dir_enabled, false);
    app_defaults_map.try_emplace(TR_KEY_watch_dir_force_generic, false);
    app_defaults_map.try_emplace(TR_KEY_rpc_enabled, true);
    app_defaults_map.try_emplace(TR_KEY_start_paused, false);
    app_defaults_map.try_emplace(TR_KEY_pidfile, tr_variant::unmanaged_string(""sv));
    auto const app_defaults = tr_variant{ std::move(app_defaults_map) };
    return tr_sessionLoadSettings(&app_defaults, config_dir, MyName);
}

} // namespace

bool tr_daemon::reopen_log_file(char const* filename)
{
    auto* const old_stream = log_stream_;

    auto* new_stream = std::fopen(filename, "a");
    if (new_stream == nullptr)
    {
        auto const err = errno;
        auto const errmsg = fmt::format(
            "Couldn't open '{path}': {error} ({error_code})",
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err));
        fmt::print(stderr, "{:s}\n", errmsg);
        return false;
    }

    log_stream_ = new_stream;

    if (old_stream != nullptr && old_stream != stderr)
    {
        fclose(old_stream);
    }

    return true;
}

void tr_daemon::report_status()
{
    double const up = tr_sessionGetRawSpeed_KBps(my_session_, TR_UP);
    double const dn = tr_sessionGetRawSpeed_KBps(my_session_, TR_DOWN);

    if (up > 0 || dn > 0)
    {
        sd_notifyf(0, "STATUS=Uploading %.2f KBps, Downloading %.2f KBps.\n", up, dn);
    }
    else
    {
        sd_notify(0, "STATUS=Idle.\n");
    }
}

void tr_daemon::periodic_update()
{
    pumpLogMessages(log_stream_);
    report_status();
}

bool tr_daemon::parse_args(int argc, char const* const* argv, bool* dump_settings, bool* foreground, int* exit_code)
{
    int c;
    char const* optstr;

    *dump_settings = false;
    *foreground = false;

    auto* const map = settings_.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    tr_optind = 1;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optstr)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'a':
            map->insert_or_assign(TR_KEY_rpc_whitelist, optstr);
            map->insert_or_assign(TR_KEY_rpc_whitelist_enabled, true);
            break;

        case 'b':
            map->insert_or_assign(TR_KEY_blocklist_enabled, true);
            break;

        case 'B':
            map->insert_or_assign(TR_KEY_blocklist_enabled, false);
            break;

        case 'c':
            map->insert_or_assign(TR_KEY_watch_dir, optstr);
            map->insert_or_assign(TR_KEY_watch_dir_enabled, true);
            break;

        case 'C':
            map->insert_or_assign(TR_KEY_watch_dir_enabled, false);
            break;

        case 941:
            map->insert_or_assign(TR_KEY_incomplete_dir, optstr);
            map->insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
            break;

        case 942:
            map->insert_or_assign(TR_KEY_incomplete_dir_enabled, false);
            break;

        case 943:
            map->insert_or_assign(TR_KEY_default_trackers, optstr);
            break;

        case 994:
            map->insert_or_assign(TR_KEY_sequential_download, true);
            break;

        case 995:
            map->insert_or_assign(TR_KEY_sequential_download, false);
            break;

        case 'd':
            *dump_settings = true;
            break;

        case 'e':
            if (reopen_log_file(optstr))
            {
                log_file_name_ = optstr;
            }

            break;

        case 'f':
            *foreground = true;
            break;

        case 'g': /* handled above */
            break;

        case 'V': /* version */
            fprintf(stderr, "%s %s\n", MyName, LONG_VERSION_STRING);
            *exit_code = 0;
            return false;

        case 'o':
            map->insert_or_assign(TR_KEY_dht_enabled, true);
            break;

        case 'O':
            map->insert_or_assign(TR_KEY_dht_enabled, false);
            break;

        case 'p':
            if (auto const rpc_port = tr_num_parse<uint16_t>(optstr); rpc_port)
            {
                map->insert_or_assign(TR_KEY_rpc_port, *rpc_port);
            }
            break;

        case 't':
            map->insert_or_assign(TR_KEY_rpc_authentication_required, true);
            break;

        case 'T':
            map->insert_or_assign(TR_KEY_rpc_authentication_required, false);
            break;

        case 'u':
            map->insert_or_assign(TR_KEY_rpc_username, optstr);
            break;

        case 'v':
            map->insert_or_assign(TR_KEY_rpc_password, optstr);
            break;

        case 'w':
            map->insert_or_assign(TR_KEY_download_dir, optstr);
            break;

        case 'P':
            if (auto const peer_port = tr_num_parse<uint16_t>(optstr); peer_port)
            {
                map->insert_or_assign(TR_KEY_peer_port, *peer_port);
            }
            break;

        case 'm':
            map->insert_or_assign(TR_KEY_port_forwarding_enabled, true);
            break;

        case 'M':
            map->insert_or_assign(TR_KEY_port_forwarding_enabled, false);
            break;

        case 'L':
            if (auto const peer_limit_global = tr_num_parse<int64_t>(optstr); peer_limit_global && *peer_limit_global >= 0)
            {
                map->insert_or_assign(TR_KEY_peer_limit_global, *peer_limit_global);
            }
            break;

        case 'l':
            if (auto const peer_limit_tor = tr_num_parse<int64_t>(optstr); peer_limit_tor && *peer_limit_tor >= 0)
            {
                map->insert_or_assign(TR_KEY_peer_limit_per_torrent, *peer_limit_tor);
            }
            break;

        case 800:
            map->insert_or_assign(TR_KEY_start_paused, true);
            break;

        case 910:
            map->insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_REQUIRED);
            break;

        case 911:
            map->insert_or_assign(TR_KEY_encryption, TR_ENCRYPTION_PREFERRED);
            break;

        case 912:
            map->insert_or_assign(TR_KEY_encryption, TR_CLEAR_PREFERRED);
            break;

        case 'i':
            map->insert_or_assign(TR_KEY_bind_address_ipv4, optstr);
            break;

        case 'I':
            map->insert_or_assign(TR_KEY_bind_address_ipv6, optstr);
            break;

        case 'r':
            map->insert_or_assign(TR_KEY_rpc_bind_address, optstr);
            break;

        case 953:
            if (auto const ratio_limit = tr_num_parse<double>(optstr); ratio_limit)
            {
                map->insert_or_assign(TR_KEY_ratio_limit, *ratio_limit);
            }
            map->insert_or_assign(TR_KEY_ratio_limit_enabled, true);
            break;

        case 954:
            map->insert_or_assign(TR_KEY_ratio_limit_enabled, false);
            break;

        case 'x':
            map->insert_or_assign(TR_KEY_pidfile, optstr);
            break;

        case 'y':
            map->insert_or_assign(TR_KEY_lpd_enabled, true);
            break;

        case 'Y':
            map->insert_or_assign(TR_KEY_lpd_enabled, false);
            break;

        case 810:
            if (auto const level = tr_logGetLevelFromKey(optstr); level)
            {
                map->insert_or_assign(TR_KEY_message_level, *level);
            }
            else
            {
                std::cerr << fmt::format(fmt::runtime(_("Couldn't parse log level '{level}'")), fmt::arg("level", optstr))
                          << std::endl;
            }
            break;

        case 811:
            std::cerr << "WARN: --log-error is deprecated. Use --log-level=error" << std::endl;
            map->insert_or_assign(TR_KEY_message_level, TR_LOG_ERROR);
            break;

        case 812:
            std::cerr << "WARN: --log-info is deprecated. Use --log-level=info" << std::endl;
            map->insert_or_assign(TR_KEY_message_level, TR_LOG_INFO);
            break;

        case 813:
            std::cerr << "WARN: --log-debug is deprecated. Use --log-level=debug" << std::endl;
            map->insert_or_assign(TR_KEY_message_level, TR_LOG_DEBUG);
            break;

        case 830:
            map->insert_or_assign(TR_KEY_utp_enabled, true);
            break;

        case 831:
            map->insert_or_assign(TR_KEY_utp_enabled, false);
            break;

        case TR_OPT_UNK:
            fprintf(stderr, "Unexpected argument: %s \n", optstr);
            tr_getopt_usage(MyName, Usage, std::data(Options));
            *exit_code = 1;
            return false;

        default:
            tr_getopt_usage(MyName, Usage, std::data(Options));
            *exit_code = 0;
            return false;
        }
    }

    return true;
}

void tr_daemon::reconfigure()
{
    if (my_session_ == nullptr)
    {
        tr_logAddInfo(_("Deferring reload until session is fully started."));
        seen_hup_ = true;
    }
    else
    {
#ifdef WITH_SYSTEMD
        auto ts = timespec{};
        if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        {
            auto error = tr_error{};
            error.set_from_errno(errno);
            tr_logAddError(
                fmt::format(
                    fmt::runtime(_("Failed to reload: Failed to get current monotonic time: {errmsg} ({errno})")),
                    fmt::arg("errmsg", error.message()),
                    fmt::arg("errno", error.code())));
            return;
        }

        sd_notifyf(
            0,
            "STATUS=Reloading...\nRELOADING=1\nMONOTONIC_USEC=%" PRIu64 "\n",
            static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL);
#endif

        char const* configDir;

        /* reopen the logfile to allow for log rotation */
        if (log_file_name_ != nullptr)
        {
            reopen_log_file(log_file_name_);
        }

        configDir = tr_sessionGetConfigDir(my_session_);
        tr_logAddInfo(fmt::format(fmt::runtime(_("Reloading settings from '{path}'")), fmt::arg("path", configDir)));

        tr_sessionSet(my_session_, load_settings(configDir));
        tr_sessionReloadBlocklists(my_session_);

        sd_notify(0, "STATUS=Reload complete.\nREADY=1\n");
    }
}

void tr_daemon::stop()
{
    event_base_loopexit(ev_base_, nullptr);
}

int tr_daemon::start([[maybe_unused]] bool foreground)
{
    sd_notifyf(0, "MAINPID=%d\n", (int)getpid());

    /* setup event state */
    ev_base_ = event_base_new();

    event* sig_ev = nullptr;
    if (ev_base_ == nullptr || !setup_signals(sig_ev))
    {
        auto const error_code = errno;
        auto const errmsg = fmt::format(
            fmt::runtime(_("Couldn't initialize daemon: {error} ({error_code})")),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code));
        printMessage(log_stream_, TR_LOG_ERROR, MyName, errmsg, __FILE__, __LINE__);
        cleanup_signals(sig_ev);
        return 1;
    }

    /* start the session */
    auto const* const cdir = this->config_dir_.c_str();
    auto* session = tr_sessionInit(cdir, true, settings_);
    tr_sessionSetRPCCallback(session, on_rpc_callback, this);
    tr_logAddInfo(fmt::format(fmt::runtime(_("Loading settings from '{path}'")), fmt::arg("path", cdir)));
    tr_sessionSaveSettings(session, cdir, settings_);

    auto const* const settings_map = settings_.get_if<tr_variant::Map>();
    if (settings_map == nullptr)
    {
        static auto constexpr Errmsg = "Unreachable code, please file a bug report"sv;
        printMessage(log_stream_, TR_LOG_ERROR, MyName, Errmsg, __FILE__, __LINE__);
        return 1;
    }

    auto const sz_pid_filename = std::string{ settings_map->value_if<std::string_view>(TR_KEY_pidfile).value_or(""sv) };
    auto pidfile_created = false;
    if (!std::empty(sz_pid_filename))
    {
        auto error = tr_error{};
        tr_sys_file_t fp = tr_sys_file_open(
            sz_pid_filename.c_str(),
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0666,
            &error);

        if (fp != TR_BAD_SYS_FILE)
        {
            auto const out = std::to_string(getpid());
            tr_sys_file_write(fp, std::data(out), std::size(out), nullptr);
            tr_sys_file_close(fp);
            tr_logAddInfo(fmt::format(fmt::runtime(_("Saved pidfile '{path}'")), fmt::arg("path", sz_pid_filename)));
            pidfile_created = true;
        }
        else
        {
            tr_logAddError(
                fmt::format(
                    fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                    fmt::arg("path", sz_pid_filename),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
        }
    }

    if (settings_map->value_if<bool>(TR_KEY_rpc_authentication_required).value_or(false))
    {
        tr_logAddInfo(_("Requiring authentication"));
    }

    my_session_ = session;

    /* If we got a SIGHUP during startup, process that now. */
    if (seen_hup_)
    {
        reconfigure();
    }

    /* maybe add a watchdir */
    auto watchdir = std::unique_ptr<Watchdir>{};
    if (settings_map->value_if<bool>(TR_KEY_watch_dir_enabled).value_or(false))
    {
        auto const force_generic = settings_map->value_if<bool>(TR_KEY_watch_dir_force_generic).value_or(false);

        if (auto dir = settings_map->value_if<std::string_view>(TR_KEY_watch_dir).value_or(""sv); !std::empty(dir))
        {
            tr_logAddInfo(fmt::format(fmt::runtime(_("Watching '{path}' for new torrent files")), fmt::arg("path", dir)));

            auto handler = [session](std::string_view dirname, std::string_view basename)
            {
                return onFileAdded(session, dirname, basename);
            };

            auto timer_maker = libtransmission::EvTimerMaker{ ev_base_ };
            watchdir = force_generic ? Watchdir::create_generic(dir, handler, timer_maker) :
                                       Watchdir::create(dir, handler, timer_maker, ev_base_);
        }
    }

    /* load the torrents */
    {
        tr_ctor* ctor = tr_ctorNew(my_session_);

        if (settings_map->value_if<bool>(TR_KEY_start_paused).value_or(false))
        {
            tr_ctorSetPaused(ctor, TR_FORCE, true);
        }

        tr_sessionLoadTorrents(my_session_, ctor);
        tr_ctorFree(ctor);
    }

#ifdef HAVE_SYSLOG

    if (!foreground)
    {
        openlog(MyName, LOG_CONS | LOG_PID, LOG_DAEMON);
    }

#endif

    /* Create new timer event to report daemon status */
    event* status_ev;
    {
        constexpr auto one_sec = timeval{ 1, 0 }; // 1 second
        status_ev = event_new(ev_base_, -1, EV_PERSIST, &::periodic_update, this);

        if (status_ev == nullptr)
        {
            auto const error_code = errno;
            tr_logAddError(
                fmt::format(
                    fmt::runtime(_("Couldn't create event: {error} ({error_code})")),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            goto CLEANUP;
        }

        if (event_add(status_ev, &one_sec) == -1)
        {
            auto const error_code = errno;
            tr_logAddError(
                fmt::format(
                    fmt::runtime(_("Couldn't add event: {error} ({error_code})")),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            goto CLEANUP;
        }
    }

    sd_notify(0, "READY=1\n");

    /* Run daemon event loop */
    if (event_base_dispatch(ev_base_) == -1)
    {
        auto const error_code = errno;
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't launch daemon event loop: {error} ({error_code})")),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
        goto CLEANUP;
    }

CLEANUP:
    sd_notify(0, "STATUS=Closing transmission session...\n");
    printf("Closing transmission session...");

    watchdir.reset();

    if (status_ev != nullptr)
    {
        event_del(status_ev);
        event_free(status_ev);
    }

    cleanup_signals(sig_ev);

    event_base_free(ev_base_);

    tr_sessionSaveSettings(my_session_, cdir, settings_);
    tr_sessionClose(my_session_);
    pumpLogMessages(log_stream_);
    printf(" done.\n");

    /* shutdown */
#ifdef HAVE_SYSLOG

    if (!foreground)
    {
        syslog(LOG_INFO, "%s", "Closing session");
        closelog();
    }

#endif

    /* cleanup */
    if (pidfile_created)
    {
        tr_sys_path_remove(sz_pid_filename);
    }

    sd_notify(0, "STATUS=\n");

    return 0;
}

bool tr_daemon::init(int argc, char const* const argv[], bool* foreground, int* ret)
{
    config_dir_ = getConfigDir(argc, argv);

    /* load settings from defaults + config file */
    settings_ = load_settings(config_dir_.c_str());

    bool dumpSettings;

    *ret = 0;

    /* overwrite settings from the command line */
    if (!parse_args(argc, argv, &dumpSettings, foreground, ret))
    {
        return false;
    }

    if (*foreground && log_stream_ == nullptr)
    {
        log_stream_ = stderr;
    }

    if (dumpSettings)
    {
        fmt::print("{:s}\n", tr_variant_serde::json().to_string(settings_));
        return false;
    }

    return true;
}

void tr_daemon::handle_error(tr_error const& error) const
{
    auto const errmsg = fmt::format("Couldn't daemonize: {:s} ({:d})", error.message(), error.code());
    printMessage(log_stream_, TR_LOG_ERROR, MyName, errmsg, __FILE__, __LINE__);
}

int tr_main(int argc, char* argv[])
{
    tr_lib_init();

    tr_locale_set_global("");

    auto foreground = bool{};
    auto ret = int{};
    auto daemon = tr_daemon{};
    if (!daemon.init(argc, argv, &foreground, &ret))
    {
        return ret;
    }

    if (auto error = tr_error{}; !daemon.spawn(foreground, &ret, error))
    {
        daemon.handle_error(error);
    }
    return ret;
}
