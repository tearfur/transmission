// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <cstddef> // size_t
#include <string>
#include <utility> // for std::make_pair()

#include "libtransmission/net.h"
#include "libtransmission/peer-transport.h"
#include "libtransmission/tr-buffer.h"

struct UTPSocket;
struct tr_session;

class tr_peer_socket
{
public:
    using InBuf = tr_peer_transport::InBuf;
    using OutBuf = tr_peer_transport::OutBuf;

    tr_peer_socket() = default;
    tr_peer_socket(
        tr_socket_address socket_address,
        bool client_is_seed,
        std::unique_ptr<tr_peer_transport::Mediator> mediator);
    tr_peer_socket(
        tr_socket_address const& socket_address,
        struct UTPSocket* sock,
        std::unique_ptr<tr_peer_transport::Mediator> mediator);
    tr_peer_socket(tr_peer_socket&& s) noexcept
    {
        *this = std::move(s);
    }
    tr_peer_socket(tr_peer_socket const&) = delete;
    tr_peer_socket& operator=(tr_peer_socket&& s) noexcept = default;
    tr_peer_socket& operator=(tr_peer_socket const&) = delete;
    ~tr_peer_socket()
    {
        close();
    }
    void close();

    size_t try_read(InBuf& buf, size_t max, tr_error* error) const;
    size_t try_write(OutBuf& buf, size_t max, tr_error* error) const;

    [[nodiscard]] TR_CONSTEXPR23 auto socket_address() const noexcept
    {
        return transport_ ? transport_->socket_address() : tr_socket_address{};
    }

    [[nodiscard]] TR_CONSTEXPR23 auto address() const noexcept
    {
        return transport_ ? transport_->address() : tr_address{};
    }

    [[nodiscard]] TR_CONSTEXPR23 auto port() const noexcept
    {
        return transport_ ? transport_->port() : tr_port{};
    }

    [[nodiscard]] std::string display_name() const
    {
        return transport_ ? transport_->display_name() : std::string{};
    }

    [[nodiscard]] constexpr auto is_utp() const noexcept
    {
        return transport_ && transport_->type() == tr_peer_transport::UTP;
    }

    [[nodiscard]] constexpr auto is_tcp() const noexcept
    {
        return transport_ && transport_->type() == tr_peer_transport::TCP;
    }

    [[nodiscard]] auto is_valid() const noexcept
    {
        return transport_ && transport_->is_valid();
    }

    [[nodiscard]] static bool limit_reached(tr_session const* session) noexcept;

private:
    std::unique_ptr<tr_peer_transport> transport_;

    static inline std::atomic<size_t> n_open_sockets_ = {};
};
