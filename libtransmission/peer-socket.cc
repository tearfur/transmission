// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::min
#include <cerrno>
#include <cstddef> // std::byte

#include <fmt/core.h>

#include <libutp/utp.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

tr_peer_socket::tr_peer_socket(
    tr_socket_address socket_address,
    bool client_is_seed,
    std::unique_ptr<tr_peer_transport::Mediator> mediator)
    : transport_{ tr_peer_tcp::create(std::move(socket_address), client_is_seed, std::move(mediator)) }
{
}

tr_peer_socket::tr_peer_socket(tr_socket_address const& socket_address, struct UTPSocket* const sock)
    : socket_address_{ socket_address }
    , type_{ Type::UTP }
{
    TR_ASSERT(sock != nullptr);

    ++n_open_sockets_;
    handle.utp = sock;

    tr_logAddTraceIo(this, fmt::format("socket (µTP) is {}", fmt::ptr(handle.utp)));
}

void tr_peer_socket::close()
{
    --n_open_sockets_;
    transport_.reset();
}

size_t tr_peer_socket::try_write(OutBuf& buf, size_t max, tr_error* error) const
{
    if (max == size_t{})
    {
        return {};
    }

    return transport_->send(buf, max, error);
}

size_t tr_peer_socket::try_read(InBuf& buf, size_t max, tr_error* error) const
{
    if (max == size_t{})
    {
        return {};
    }

    return transport_->recv(buf, max, error);
}

bool tr_peer_socket::limit_reached(tr_session const* const session) noexcept
{
    return n_open_sockets_.load() >= session->peerLimit();
}
