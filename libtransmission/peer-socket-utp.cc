// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include <libutp/utp.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-utp.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-buffer.h"
#include "libtransmission/utils-ev.h"

#define tr_logAddErrorSock(sock, msg) tr_logAddError(msg, (sock)->display_name())
#define tr_logAddTraceSock(sock, msg) tr_logAddTrace(msg, (sock)->display_name())

namespace
{
#ifdef WITH_UTP
void max_bufsize(tr_socket_t const fd, int const optname, int& ret)
{
    static auto constexpr IntMax = std::numeric_limits<int>::max();
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &IntMax, sizeof(IntMax)) < 0)
    {
        tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF to max on socket {}: {}", fd, tr_net_strerror(sockerrno)));
    }

    auto tmp = int{};
    socklen_t len = sizeof(tmp);
    if (getsockopt(fd, SOL_SOCKET, optname, reinterpret_cast<char*>(&tmp), &len) == 0)
    {
        ret = std::max(tmp, ret);
    }
}

int max_udp_bufsize(int const optname)
{
    int ret = -1;

    if (auto const fd = socket(PF_INET, SOCK_DGRAM, 0); fd != TR_BAD_SOCKET)
    {
        max_bufsize(fd, optname, ret);
        tr_net_close_socket(fd);
    }

    if (auto const fd = socket(PF_INET6, SOCK_DGRAM, 0); fd != TR_BAD_SOCKET)
    {
        max_bufsize(fd, optname, ret);
        tr_net_close_socket(fd);
    }

    tr_logAddTrace(fmt::format("max UDP option {} was {}", optname, ret));
    return ret;
}

class tr_peer_socket_utp_impl final
    : public tr_peer_socket_utp
    , public std::enable_shared_from_this<tr_peer_socket_utp_impl>
{
public:
    tr_peer_socket_utp_impl(tr_session const& session, tr_socket_address const& socket_address, UTPSocket* sock)
        : tr_peer_socket_utp{ socket_address }
        , sock_{ sock }
    {
        TR_ASSERT(sock != nullptr);

        utp_set_userdata(sock_, this);

        event_read_.reset(event_new(session.event_base(), -1, 0, event_read_cb, this));

        tr_logAddTraceSock(this, fmt::format("socket (µTP) is {}", fmt::ptr(sock_)));
    }

    ~tr_peer_socket_utp_impl() override
    {
        event_read_.reset();
        utp_set_userdata(sock_, nullptr);
        utp_close(sock_);
    }

    [[nodiscard]] Type type() const noexcept override
    {
        return Type::UTP;
    }

    void set_read_enabled(bool const enabled) override
    {
        is_read_enabled_ = enabled;
        maybe_set_read_active();
    }

    void set_write_enabled(bool const enabled) override
    {
        is_write_enabled_ = enabled;
    }

    [[nodiscard]] bool is_read_enabled() const override
    {
        return is_read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const override
    {
        return is_write_enabled_;
    }

    [[nodiscard]] constexpr auto& read_buffer() noexcept
    {
        return inbuf_;
    }

    [[nodiscard]] auto read_buffer_size() const noexcept
    {
        return std::size(inbuf_);
    }

    void maybe_set_read_active() noexcept
    {
        if (is_read_enabled() && !std::empty(read_buffer()))
        {
            event_active(event_read_.get(), 0, 0);
        }
    }

    // --- libutp

    void on_utp_state_change(int state) const
    {
        switch (state)
        {
        case UTP_STATE_CONNECT:
            tr_logAddTraceSock(this, "utp_on_state_change -- changed to connected");
            break;
        case UTP_STATE_WRITABLE:
            tr_logAddTraceSock(this, "utp_on_state_change -- changed to writable");
            if (is_write_enabled())
            {
                write_cb();
            }
            break;
        case UTP_STATE_EOF:
            {
                auto error = tr_error{};
                error.set_from_errno(ENOTCONN);
                error_cb(error);
            }
            break;
        case UTP_STATE_DESTROYING:
            tr_logAddErrorSock(this, "Impossible state UTP_STATE_DESTROYING");
            break;
        default:
            tr_logAddErrorSock(this, fmt::format(fmt::runtime(_("Unknown state: {state}")), fmt::arg("state", state)));
            break;
        }
    }

    void on_utp_error(int errcode) const
    {
        tr_logAddTraceSock(this, fmt::format("on_utp_error -- {}", utp_error_code_names[errcode]));

        if (!error_cb_)
        {
            return;
        }

        auto error = tr_error{};
        switch (errcode)
        {
        case UTP_ECONNREFUSED:
            error.set_from_errno(ECONNREFUSED);
            break;
        case UTP_ECONNRESET:
            error.set_from_errno(ECONNRESET);
            break;
        case UTP_ETIMEDOUT:
            error.set_from_errno(ETIMEDOUT);
            break;
        default:
            error.set(errcode, utp_error_code_names[errcode]);
            break;
        }

        error_cb_(error);
    }

private:
    static void event_read_cb(evutil_socket_t /*fd*/, short /*event*/, void* vs)
    {
        auto const s = static_cast<tr_peer_socket_utp_impl*>(vs)->shared_from_this();
        tr_logAddTraceSock(s, "this µTP socket is ready for reading");

        s->is_read_enabled_ = false;
        s->read_cb();

        s->maybe_set_read_active();
    }

    size_t try_read_impl(InBuf& buf, size_t const max, tr_error* /*error*/) override
    {
        auto const len = std::min(max, read_buffer_size());
        buf.add(std::data(inbuf_), len);
        inbuf_.drain(len);
        return len;
    }

    size_t try_write_impl(OutBuf& buf, size_t const max, tr_error* error) override
    {
        set_sockerrno(0);
        auto const n_to_write = std::min(max, std::size(buf));
        // NB: utp_write() does not modify its 2nd arg, but a wart in
        // libutp's public API requires it to be non-const anyway :shrug:
        auto const n_written = utp_write(sock_, const_cast<std::byte*>(std::data(buf)), n_to_write);
        auto const error_code = sockerrno;

        if (n_written > 0)
        {
            buf.drain(n_written);
            return static_cast<size_t>(n_written);
        }

        if (error != nullptr && n_written < 0 && error_code != 0)
        {
            error->set(error_code, tr_net_strerror(error_code));
        }

        return {};
    }

    // ---

    UTPSocket* sock_;

    // This buffer acts in place of the OS's receive buffer.
    // Care should be taken to have it mimic that behaviour.
    PeerBuffer inbuf_;

    libtransmission::evhelpers::event_unique_ptr event_read_;

    bool is_read_enabled_ = false;
    bool is_write_enabled_ = false;
};
#endif
} // namespace

tr_peer_socket_utp::tr_peer_socket_utp(tr_socket_address const& socket_address)
    : tr_peer_socket{ socket_address }
{
}

std::shared_ptr<tr_peer_socket_utp> tr_peer_socket_utp::create(
    tr_session const& session,
    tr_socket_address const& socket_address,
    UTPSocket* sock)
{
#ifdef WITH_UTP
    return std::make_unique<tr_peer_socket_utp_impl>(session, socket_address, sock);
#else
    return {};
#endif
}

std::shared_ptr<tr_peer_socket_utp> tr_peer_socket_utp::create(
    tr_session const& session,
    tr_socket_address const& socket_address,
    struct_utp_context* ctx)
{
#ifdef WITH_UTP
    auto* const sock = utp_create_socket(ctx);
    auto const [ss, sslen] = socket_address.to_sockaddr();
    if (utp_connect(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) == 0)
    {
        return std::make_unique<tr_peer_socket_utp_impl>(session, socket_address, sock);
    }
#endif
    return {};
}

void tr_peer_socket_utp::utp_init([[maybe_unused]] struct_utp_context* ctx)
{
#ifdef WITH_UTP
    // Mimic OS UDP socket buffer
    if (auto const rcvbuf = max_udp_bufsize(SO_RCVBUF); rcvbuf > 0)
    {
        utp_context_set_option(ctx, UTP_RCVBUF, rcvbuf);
    }
    if (auto const sndbuf = max_udp_bufsize(SO_SNDBUF); sndbuf > 0)
    {
        utp_context_set_option(ctx, UTP_SNDBUF, sndbuf);
    }

    // note: all the callback handlers here need to check `userdata` for nullptr
    // because libutp can fire callbacks on a socket after utp_close() is called

    utp_set_callback(
        ctx,
        UTP_ON_READ,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->read_buffer().add(args->buf, args->len);
                s->maybe_set_read_active();

                // utp_read_drained() notifies libutp that we read a packet from them.
                // It opens up the congestion window by sending an ACK (soonish) if
                // one was not going to be sent.
                utp_read_drained(args->socket);
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_GET_READ_BUFFER_SIZE,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                return s->read_buffer_size();
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_ERROR,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->on_utp_error(args->error_code);
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_STATE_CHANGE,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->on_utp_state_change(args->state);
            }
            return {};
        });
#endif
}
