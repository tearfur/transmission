// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#ifndef _WIN32
#include <netinet/tcp.h> // TCP_CONGESTION
#endif

#include <event2/event.h>

#include <fmt/core.h>

#include "libtransmission/transmission.h"

#include "libtransmission/error.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-transport.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/log.h"
#include "libtransmission/utils-ev.h"

#define tr_logAddErrorTcp(tcp, msg) tr_logAddError(msg, (tcp)->display_name())
#define tr_logAddWarnTcp(tcp, msg) tr_logAddWarn(msg, (tcp)->display_name())
#define tr_logAddDebugTcp(tcp, msg) tr_logAddDebug(msg, (tcp)->display_name())
#define tr_logAddTraceTcp(tcp, msg) tr_logAddTrace(msg, (tcp)->display_name())

namespace
{
tr_socket_t create_socket(int domain, int type)
{
    auto const sockfd = socket(domain, type, 0);
    if (sockfd == TR_BAD_SOCKET)
    {
        if (auto err = sockerrno; err != EAFNOSUPPORT)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't create socket: {error} ({error_code})"),
                fmt::arg("error", tr_net_strerror(err)),
                fmt::arg("error_code", err)));
        }

        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(sockfd) == -1)
    {
        tr_net_close_socket(sockfd);
        return TR_BAD_SOCKET;
    }

    if (static bool buf_logged = false; !buf_logged)
    {
        int i = 0;
        socklen_t size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_SNDBUF size is {}", i));
        }

        i = 0;
        size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_RCVBUF size is {}", i));
        }

        buf_logged = true;
    }

    return sockfd;
}

tr_socket_t open_peer_socket(tr_address bind_address, tr_socket_address const& socket_address, bool client_is_seed)
{
    auto const& [addr, port] = socket_address;

    TR_ASSERT(socket_address.is_valid());
    if (!socket_address.is_valid())
    {
        return TR_BAD_SOCKET;
    }

    auto const s = create_socket(tr_ip_protocol_to_af(addr.type), SOCK_STREAM);
    if (s == TR_BAD_SOCKET)
    {
        return TR_BAD_SOCKET;
    }

    // seeds don't need a big read buffer, so make it smaller
    if (client_is_seed)
    {
        int const n = 8192;

        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&n), sizeof(n)) == -1)
        {
            tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF on socket {}: {}", s, tr_net_strerror(sockerrno)));
        }
    }

    if (auto const [src_ss, src_sslen] = tr_socket_address::to_sockaddr(bind_address, {});
        bind(s, reinterpret_cast<sockaddr const*>(&src_ss), src_sslen) == -1)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't set source address {address} on {socket}: {error} ({error_code})"),
            fmt::arg("address", bind_address.display_name()),
            fmt::arg("socket", s),
            fmt::arg("error", tr_net_strerror(sockerrno)),
            fmt::arg("error_code", sockerrno)));
        tr_net_close_socket(s);
        return TR_BAD_SOCKET;
    }

    if (auto const [tgt_ss, tgt_sslen] = socket_address.to_sockaddr();
        connect(s, reinterpret_cast<sockaddr const*>(&tgt_ss), tgt_sslen) == -1 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        if (auto const tmperrno = sockerrno;
            (tmperrno != ECONNREFUSED && tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr.is_ipv4())
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't connect socket {socket} to {address}:{port}: {error} ({error_code})"),
                fmt::arg("socket", s),
                fmt::arg("address", addr.display_name()),
                fmt::arg("port", port.host()),
                fmt::arg("error", tr_net_strerror(tmperrno)),
                fmt::arg("error_code", tmperrno)));
        }

        tr_net_close_socket(s);
        return TR_BAD_SOCKET;
    }

    tr_logAddTrace(fmt::format("New OUTGOING connection {} ({})", s, socket_address.display_name()));

    return s;
}

void set_tos(tr_socket_t s, [[maybe_unused]] int tos, tr_address_type type)
{
    if (s == TR_BAD_SOCKET)
    {
        return;
    }

    if (type == TR_AF_INET)
    {
#if defined(IP_TOS) && !defined(_WIN32)
        if (setsockopt(s, IPPROTO_IP, IP_TOS, reinterpret_cast<char const*>(&tos), sizeof(tos)) == -1)
        {
            tr_logAddDebug(fmt::format("Can't set TOS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    }
    else if (type == TR_AF_INET6)
    {
#if defined(IPV6_TCLASS) && !defined(_WIN32)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, reinterpret_cast<char const*>(&tos), sizeof(tos)) == -1)
        {
            tr_logAddDebug(fmt::format("Can't set IPv6 ToS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    }
    else
    {
        /* program should never reach here! */
        tr_logAddDebug("Something goes wrong while setting TOS/Traffic-Class");
    }
}

void set_congestion_control([[maybe_unused]] tr_socket_t s, [[maybe_unused]] std::string const& algorithm)
{
#ifdef TCP_CONGESTION
    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, algorithm.c_str(), std::size(algorithm) + 1) == -1)
    {
        tr_logAddDebug(fmt::format("Can't set congestion control algorithm '{}': {}", algorithm, tr_net_strerror(sockerrno)));
    }
#endif
}

class tr_peer_tcp_impl final : public tr_peer_tcp
{
public:
    tr_peer_tcp_impl(tr_socket_address const& socket_address, bool client_is_seed, std::unique_ptr<Mediator> mediator)
        : tr_peer_tcp{ socket_address }
        , fd_{ open_peer_socket(mediator->bind_address(address().type), socket_address, client_is_seed) }
        , mediator_{ std::move(mediator) }
    {
        if (!is_valid())
        {
            tr_logAddTraceTcp(this, "failed to create socket (tcp)");
            return;
        }

        set_tos(fd_, mediator_->tos(), address().type);

        if (auto const& alg = mediator_->peer_congestion_algorithm(); !std::empty(alg))
        {
            set_congestion_control(fd_, alg);
        }

        event_read_.reset(event_new(mediator_->event_base(), fd_, EV_READ, event_read_cb, this));
        event_write_.reset(event_new(mediator_->event_base(), fd_, EV_WRITE, event_write_cb, this));

        tr_logAddTraceTcp(this, fmt::format("socket (tcp) is {}", fd_));
    }

    ~tr_peer_tcp_impl() override
    {
        tr_net_close_socket(fd_);
        fd_ = TR_BAD_SOCKET;
    }

    // ---

    [[nodiscard]] bool is_valid() const noexcept override
    {
        return tr_peer_transport::is_valid() && fd_ != TR_BAD_SOCKET;
    }

    [[nodiscard]] Type type() const noexcept override
    {
        return TCP;
    }

    // ---

    size_t recv(InBuf& buf, size_t n_bytes, tr_error* error = nullptr) override
    {
        if (n_bytes == 0U)
        {
            return {};
        }

        auto const [p_buf, buflen] = buf.reserve_space(n_bytes);
        n_bytes = std::min(n_bytes, buflen);
        auto const n_read = ::recv(fd_, reinterpret_cast<char*>(p_buf), n_bytes, 0);
        auto const err = sockerrno;

        if (n_read > 0)
        {
            auto const ret = static_cast<size_t>(n_read);
            mediator_->notify_bandwidth_overhead(TR_DOWN, guess_packet_overhead(ret));
            buf.commit_space(n_read);
            return static_cast<size_t>(ret);
        }

        // When a stream socket peer has performed an orderly shutdown,
        // the return value will be 0 (the traditional "end-of-file" return).
        if (error != nullptr)
        {
            if (n_read == 0)
            {
                error->set_from_errno(ENOTCONN);
            }
            else
            {
                error->set(err, tr_net_strerror(err));
            }
        }

        return {};
    }

    size_t send(OutBuf& buf, size_t n_bytes, tr_error* error = nullptr) override
    {
        n_bytes = std::min(n_bytes, buf.size());

        if (n_bytes == 0U)
        {
            return {};
        }

        if (auto const n_sent = ::send(fd_, reinterpret_cast<char const*>(buf.data()), n_bytes, 0); n_sent >= 0U)
        {
            auto const ret = static_cast<size_t>(n_sent);
            mediator_->notify_bandwidth_overhead(TR_UP, guess_packet_overhead(ret));
            buf.drain(n_sent);
            return ret;
        }

        if (error != nullptr)
        {
            auto const err = sockerrno;
            error->set(err, tr_net_strerror(err));
        }

        return {};
    }

    // ---

    void set_enabled(tr_direction dir, bool is_enabled) override
    {
        TR_ASSERT(tr_isDirection(dir));

        short const event = dir == TR_UP ? EV_WRITE : EV_READ;

        if (is_enabled)
        {
            event_enable(event);
        }
        else
        {
            event_disable(event);
        }
    }

private:
    void event_enable(short event)
    {
        TR_ASSERT(event_read_);
        TR_ASSERT(event_write_);

        if ((event & EV_READ) != 0 && (pending_events_ & EV_READ) == 0)
        {
            tr_logAddTraceTcp(this, "enabling ready-to-read polling");
            event_add(event_read_.get(), nullptr);
            pending_events_ |= EV_READ;
        }

        if ((event & EV_WRITE) != 0 && (pending_events_ & EV_WRITE) == 0)
        {
            tr_logAddTraceTcp(this, "enabling ready-to-write polling");
            event_add(event_write_.get(), nullptr);
            pending_events_ |= EV_WRITE;
        }
    }

    void event_disable(short event)
    {
        TR_ASSERT(event_read_);
        TR_ASSERT(event_write_);

        if ((event & EV_READ) != 0 && (pending_events_ & EV_READ) != 0)
        {
            tr_logAddTraceTcp(this, "disabling ready-to-read polling");
            event_del(event_read_.get());
            pending_events_ &= ~EV_READ;
        }

        if ((event & EV_WRITE) != 0 && (pending_events_ & EV_WRITE) != 0)
        {
            tr_logAddTraceTcp(this, "disabling ready-to-write polling");
            event_del(event_write_.get());
            pending_events_ &= ~EV_WRITE;
        }
    }

    static void event_read_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vtcp)
    {
        auto* const tcp = static_cast<tr_peer_tcp_impl*>(vtcp);
        tr_logAddTraceTcp(tcp, "libevent says this TCP peer socket is ready for reading");

        TR_ASSERT(tcp->fd_ == fd);

        tcp->pending_events_ &= ~EV_READ;

        tcp->mediator_->notify_read();
    }

    static void event_write_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vtcp)
    {
        auto* const tcp = static_cast<tr_peer_tcp_impl*>(vtcp);
        tr_logAddTraceTcp(tcp, "libevent says this TCP peer socket is ready for writing");

        TR_ASSERT(tcp->fd_ == fd);

        tcp->pending_events_ &= ~EV_WRITE;

        tcp->mediator_->notify_write();
    }

    // ---

    [[nodiscard]] constexpr size_t guess_packet_overhead(size_t n_bytes) const noexcept
    {
        // https://web.archive.org/web/20140912230020/http://sd.wareonearth.com:80/~phil/net/overhead/
        // TCP over Ethernet:
        // Assuming no header compression (e.g. not PPP)
        // Add 20 IPv4 header or 40 IPv6 header (no options)
        // Add 20 TCP header
        // Add 12 bytes optional TCP timestamps
        // Max TCP Payload data rates over ethernet are thus:
        // (1500-40)/ (38+1500) = 94.9285 %  IPv4, minimal headers
        // (1500-52)/ (38+1500) = 94.1482 %  IPv4, TCP timestamps
        // (1500-52)/ (42+1500) = 93.9040 %  802.1q, IPv4, TCP timestamps
        // (1500-60)/ (38+1500) = 93.6281 %  IPv6, minimal headers
        // (1500-72)/ (38+1500) = 92.8479 %  IPv6, TCP timestamps
        // (1500-72)/ (42+1500) = 92.6070 %  802.1q, IPv6, TCP timestamps

        // So, let's guess around 7% overhead
        return n_bytes / 14U;
    }

    short pending_events_ = {};

    tr_socket_t fd_ = TR_BAD_SOCKET;

    libtransmission::evhelpers::event_unique_ptr event_read_;
    libtransmission::evhelpers::event_unique_ptr event_write_;

    std::unique_ptr<Mediator> mediator_;
};
} // namespace

std::unique_ptr<tr_peer_tcp> tr_peer_tcp::create(
    tr_socket_address const& socket_address,
    bool client_is_seed,
    std::unique_ptr<Mediator> mediator)
{
    return std::make_unique<tr_peer_tcp_impl>(socket_address, client_is_seed, std::move(mediator));
}
