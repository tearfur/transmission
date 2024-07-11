// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/peer-transport.h"

namespace
{
class tr_peer_utp_impl final : public tr_peer_utp
{
    tr_peer_utp_impl()
        : tr_peer_utp{ {} }
    {
    }

    size_t recv(tr_peer_transport::InBuf& buf, size_t n_bytes, tr_error* error = nullptr) override
    {
        return 0;
    }

    size_t send(tr_peer_transport::OutBuf& buf, size_t n_bytes, tr_error* error = nullptr) override
    {
        return 0;
    }

    void set_enabled(tr_direction dir, bool is_enabled) override
    {
    }
};
} // namespace

std::unique_ptr<tr_peer_utp> tr_peer_utp::create(tr_socket_address const& socket_address, std::unique_ptr<Mediator> mediator)
{
    return {};
}
