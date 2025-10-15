// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdio>
#include <string>

#ifdef HAVE_SYS_SIGNALFD_H
#include <unistd.h>
#endif

#include <libtransmission/variant.h>
#include <libtransmission/quark.h>

struct event_base;
struct tr_error;
struct tr_session;

class tr_daemon
{
public:
    tr_daemon() = default;

    ~tr_daemon()
    {
#ifdef HAVE_SYS_SIGNALFD_H
        if (sigfd_ != -1)
        {
            close(sigfd_);
        }
#endif /* signalfd API */
    }

    bool spawn(bool foreground, int* exit_code, tr_error& error);
    bool init(int argc, char const* const argv[], bool* foreground, int* ret);
    void handle_error(tr_error const&) const;
    int start(bool foreground);
    void periodic_update();
    void reconfigure();
    void stop();

private:
#ifdef HAVE_SYS_SIGNALFD_H
    int sigfd_ = -1;
#endif /* signalfd API */
    bool seen_hup_ = false;
    std::string config_dir_;
    tr_variant settings_ = {};
    tr_session* my_session_ = nullptr;
    char const* log_file_name_ = nullptr;
    struct event_base* ev_base_ = nullptr;
    FILE* log_stream_ = nullptr;

    bool parse_args(int argc, char const* const* argv, bool* dump_settings, bool* foreground, int* exit_code);
    bool reopen_log_file(char const* filename);
    bool setup_signals(struct event*& sig_ev);
    void cleanup_signals(struct event* sig_ev) const;
    void report_status();
};
