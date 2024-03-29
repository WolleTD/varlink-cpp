#ifndef LIBVARLINK_ASYNC_SERVER_HPP
#define LIBVARLINK_ASYNC_SERVER_HPP

#include <variant>
#include <experimental/filesystem>
#include <varlink/server_session.hpp>

namespace varlink {
template <typename Protocol>
class async_server : public std::enable_shared_from_this<async_server<Protocol>> {
  public:
    using protocol_type = Protocol;
    using acceptor_type = typename protocol_type::acceptor;
    using socket_type = typename protocol_type::socket;
    using executor_type = typename acceptor_type::executor_type;
    using session_type = server_session<protocol_type>;

    using std::enable_shared_from_this<async_server<Protocol>>::shared_from_this;

    executor_type get_executor() { return acceptor_.get_executor(); }

  private:
    acceptor_type acceptor_;
    varlink_service& service_;

  public:
    explicit async_server(acceptor_type acceptor, varlink_service& service)
        : acceptor_(std::move(acceptor)), service_(service)
    {
    }

    async_server(const async_server& src) = delete;
    async_server& operator=(const async_server&) = delete;
    async_server(async_server&& src) noexcept = default;
    async_server& operator=(async_server&& src) noexcept = default;

    template <typename ConnectionHandler>
    auto async_accept(ConnectionHandler&& handler)
    {
        return net::async_initiate<ConnectionHandler, void(std::error_code, std::shared_ptr<session_type>)>(
            async_accept_initiator(this), handler);
    }

    void async_serve_forever()
    {
        async_accept([this](auto ec, auto session) {
            if (ec) { return; }
            session->start();
            async_serve_forever();
        });
    }

    ~async_server()
    {
        using namespace std::experimental::filesystem;
        if constexpr (std::is_same_v<protocol_type, net::local::stream_protocol>) {
            if (acceptor_.is_open()) { remove(acceptor_.local_endpoint().path()); }
        }
    }

  private:
    class async_accept_initiator {
      private:
        async_server* self_;

      public:
        explicit async_accept_initiator(async_server* self) : self_(self) {}

        template <typename ConnectionHandler>
        void operator()(ConnectionHandler&& handler)
        {
            self_->acceptor_.async_accept(
                [self = self_, handler_ = std::forward<ConnectionHandler>(handler)](
                    std::error_code ec, socket_type socket) mutable {
                    std::shared_ptr<session_type> session{};
                    if (!ec) {
                        session = std::make_shared<session_type>(std::move(socket), self->service_);
                    }
                    handler_(ec, std::move(session));
                });
        }
    };
};

using async_server_unix = async_server<net::local::stream_protocol>;
using async_server_tcp = async_server<net::ip::tcp>;
using async_server_variant = std::variant<async_server_unix, async_server_tcp>;

} // namespace varlink
#endif // LIBVARLINK_ASYNC_SERVER_HPP
