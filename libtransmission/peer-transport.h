// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "libtransmission/transmission.h"

#include "libtransmission/net.h"
#include "libtransmission/tr-buffer.h"

struct event_base;

class tr_peer_transport
{
public:
    using InBuf = libtransmission::BufferWriter<std::byte>;
    using OutBuf = libtransmission::BufferReader<std::byte>;

    enum Type : uint8_t
    {
        UTP,
        TCP
    };

    struct Mediator
    {
        virtual ~Mediator() = default;

        virtual void notify_read() = 0;
        virtual void notify_write() = 0;
        virtual void notify_bandwidth_overhead(tr_direction dir, size_t n_bytes) = 0;

        virtual event_base* event_base() = 0;

        virtual std::string const& peer_congestion_algorithm() = 0;
        virtual tr_address bind_address(tr_address_type type) = 0;
        virtual tr_tos_t tos() = 0;
    };

    explicit tr_peer_transport(tr_socket_address const& socket_address)
        : socket_address_{ socket_address }
    {
    }

    virtual ~tr_peer_transport() = default;

    [[nodiscard]] virtual bool is_valid() const noexcept
    {
        return socket_address_.is_valid();
    }

    [[nodiscard]] constexpr auto const& socket_address() const noexcept
    {
        return socket_address_;
    }

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return socket_address_.address();
    }

    [[nodiscard]] constexpr auto port() const noexcept
    {
        return socket_address_.port();
    }

    [[nodiscard]] auto display_name() const
    {
        return socket_address_.display_name();
    }

    [[nodiscard]] virtual Type type() const noexcept = 0;

    virtual size_t recv(InBuf& buf, size_t n_bytes, tr_error* error = nullptr) = 0;
    virtual size_t send(OutBuf& buf, size_t n_bytes, tr_error* error = nullptr) = 0;

    virtual void set_enabled(tr_direction dir, bool is_enabled) = 0;

protected:
    tr_socket_address socket_address_;
};

struct tr_peer_tcp : public tr_peer_transport
{
    explicit tr_peer_tcp(tr_socket_address const& socket_address)
        : tr_peer_transport{ socket_address }
    {
    }

    static std::unique_ptr<tr_peer_tcp> create(
        tr_socket_address const& socket_address,
        bool client_is_seed,
        std::unique_ptr<Mediator> mediator);
};

struct tr_peer_utp : public tr_peer_transport
{
    explicit tr_peer_utp(tr_socket_address const& socket_address)
        : tr_peer_transport{ socket_address }
    {
    }

    static std::unique_ptr<tr_peer_utp> create(tr_socket_address const& socket_address, std::unique_ptr<Mediator> mediator);
};
