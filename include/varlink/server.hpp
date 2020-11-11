/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <charconv>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename Socket>
class server_session
    : public json_connection<Socket>,
      public std::enable_shared_from_this<server_session<Socket>> {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<socket_type>;

    using std::enable_shared_from_this<server_session<Socket>>::shared_from_this;
    using json_connection<Socket>::async_send;
    using json_connection<Socket>::async_receive;

  public:
    explicit server_session(socket_type socket)
        : json_connection<Socket>(std::move(socket))
    {
    }

    server_session(const server_session&) = delete;
    server_session& operator=(const server_session&) = delete;
    server_session(server_session&&) noexcept = default;
    server_session& operator=(server_session&&) noexcept = default;

    template <typename MessageHandler>
    void start(MessageHandler&& handler)
    {
        async_receive([self = shared_from_this(), handler](auto ec, auto&& j) {
            if (ec) {
                return;
            }
            try {
                const auto reply = handler(j);
                if (reply.is_object()) {
                    self->async_send(reply, asio::use_future);
                }
                self->start(handler);
            }
            catch (...) {
            }
        });
    }
};

template <typename Acceptor>
class async_server : public std::enable_shared_from_this<async_server<Acceptor>> {
  public:
    using acceptor_type = Acceptor;
    using protocol_type = typename acceptor_type::protocol_type;
    using socket_type = typename protocol_type::socket;
    using executor_type = typename acceptor_type::executor_type;
    using session_type = server_session<socket_type>;

    using std::enable_shared_from_this<async_server<Acceptor>>::shared_from_this;

  private:
    acceptor_type acceptor_;

  public:
    explicit async_server(acceptor_type acceptor)
        : acceptor_(std::move(acceptor))
    {
    }

    async_server(const async_server& src) = delete;
    async_server& operator=(const async_server&) = delete;
    async_server(async_server&& src) noexcept = default;
    async_server& operator=(async_server&& src) noexcept = default;

    template <ASIO_COMPLETION_TOKEN_FOR(
        void(asio::error_code, std::shared_ptr<connection_type>))
                  ConnectionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_accept(
        ConnectionHandler&& handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return asio::async_initiate<
            ConnectionHandler,
            void(asio::error_code, std::shared_ptr<session_type>)>(
            async_accept_initiator(this), handler);
    }

    void async_serve_forever(varlink_service& service)
    {
        async_accept([this, &service](auto ec, auto session) {
            if (ec) {
                return;
            }
            session->start([session, &service](auto&& j) {
                const varlink_message message{j};
                return service.message_call(message, [session](auto&& more) {
                    session->async_send(more, asio::use_future);
                });
            });
            async_serve_forever(service);
        });
    }

    ~async_server()
    {
        if constexpr (std::is_same_v<protocol_type, asio::local::stream_protocol>) {
            if (acceptor_.is_open()) {
                std::filesystem::remove(acceptor_.local_endpoint().path());
            }
        }
    }

  private:
    class async_accept_initiator {
      private:
        async_server<Acceptor>* self_;

      public:
        explicit async_accept_initiator(async_server<Acceptor>* self)
            : self_(self)
        {
        }

        template <typename ConnectionHandler>
        void operator()(ConnectionHandler&& handler)
        {
            self_->acceptor_.async_accept(
                [handler_ = std::forward<ConnectionHandler>(handler)](
                    asio::error_code ec, socket_type socket) mutable {
                    std::shared_ptr<session_type> session;
                    if (!ec) {
                        session = std::make_shared<session_type>(
                            std::move(socket));
                    }
                    handler_(ec, std::move(session));
                });
        }
    };
};

using async_server_unix = async_server<asio::local::stream_protocol::acceptor>;
using async_server_tcp = async_server<asio::ip::tcp::acceptor>;
using async_server_variant = std::variant<async_server_unix, async_server_tcp>;

class threaded_server {
  private:
    asio::thread_pool ctx;
    async_server_variant server;
    varlink_service service;

    auto make_async_server(const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> async_server_variant {
                using Acceptor =
                    typename std::decay_t<decltype(sockaddr)>::protocol_type::acceptor;
                return async_server(Acceptor{ctx, sockaddr});
            },
            endpoint_from_uri(uri));
    }

  public:
    threaded_server(
        const varlink_uri& uri,
        const varlink_service::description& description)
        : ctx(4), server(make_async_server(uri)), service(description)
    {
        std::visit(
            [&](auto&& s) {
                asio::post(ctx, [&]() { s.async_serve_forever(service); });
            },
            server);
    }

    threaded_server(std::string_view uri, const varlink_service::description& description)
        : threaded_server(varlink_uri(uri), description)
    {
    }

    threaded_server(const threaded_server& src) = delete;
    threaded_server& operator=(const threaded_server&) = delete;
    threaded_server(threaded_server&& src) noexcept = delete;
    threaded_server& operator=(threaded_server&& src) noexcept = delete;

    template <typename... Args>
    void add_interface(Args&&... args)
    {
        service.add_interface(std::forward<Args>(args)...);
    }

    void stop() { ctx.stop(); }

    void join() { ctx.join(); }
};
} // namespace varlink

#endif
