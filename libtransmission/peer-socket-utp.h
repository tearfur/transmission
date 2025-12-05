// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TRANSMISSION_PEER_SOCKET_UTP_H
#define TRANSMISSION_PEER_SOCKET_UTP_H

#include <memory>

#include "libtransmission/peer-socket.h"

struct struct_utp_context;
struct tr_socket_address;
struct UTPSocket;

class tr_peer_socket_utp : public tr_peer_socket
{
public:
    [[nodiscard]] static std::unique_ptr<tr_peer_socket_utp> create(tr_socket_address const& socket_address, UTPSocket* sock);
    [[nodiscard]] static std::unique_ptr<tr_peer_socket_utp> create(
        tr_socket_address const& socket_address,
        struct_utp_context* ctx);

    static void utp_init(struct_utp_context* ctx);

protected:
    explicit tr_peer_socket_utp(tr_socket_address const& socket_address);
};

#endif //TRANSMISSION_PEER_SOCKET_UTP_H
