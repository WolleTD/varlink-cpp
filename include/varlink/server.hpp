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
    : public std::enable_shared_from_this<server_session<Socket>> {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<socket_type>;

    using std::enable_shared_from_this<server_session<Socket>>::shared_from_this;

    socket_type& socket() { return connection.socket(); }
    [[nodiscard]] const socket_type& socket() const { return connection.socket(); }

    executor_type get_executor() { return socket().get_executor(); }

  private:
    json_connection<socket_type> connection;
    varlink_service& service_;

  public:
    explicit server_session(socket_type socket, varlink_service& service)
        : connection(std::move(socket)), service_(service)
    {
    }

    server_session(const server_session&) = delete;
    server_session& operator=(const server_session&) = delete;
    server_session(server_session&&) noexcept = default;
    server_session& operator=(server_session&&) noexcept = default;

    void start()
    {
        connection.async_receive([self = shared_from_this()](auto ec, auto&& j) {
            if (ec)
                return;
            try {
                const varlink_message message{j};
                auto reply = std::make_unique<json>(self->service_.message_call(
                    message, [self](const json& more) {
                        auto m = std::make_unique<json>(more);
                        self->async_send_reply(std::move(m));
                    }));
                if (reply->is_object()) {
                    self->async_send_reply(std::move(reply));
                }
                self->start();
            }
            catch (...) {
            }
        });
    }

  private:
    void async_send_reply(std::unique_ptr<json> message)
    {
        auto& data = *message;
        connection.async_send(
            data, [m = std::move(message), self = shared_from_this()](auto ec) {
                if (ec)
                    self->connection.socket().cancel();
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

    template <ASIO_COMPLETION_TOKEN_FOR(
        void(std::error_code, std::shared_ptr<connection_type>))
                  ConnectionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_accept(
        ConnectionHandler&& handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return net::async_initiate<
            ConnectionHandler,
            void(std::error_code, std::shared_ptr<session_type>)>(
            async_accept_initiator(this), handler);
    }

    void async_serve_forever()
    {
        async_accept([this](auto ec, auto session) {
            if (ec) {
                return;
            }
            session->start();
            async_serve_forever();
        });
    }

    ~async_server()
    {
        if constexpr (std::is_same_v<protocol_type, net::local::stream_protocol>) {
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
                [self = self_,
                 handler_ = std::forward<ConnectionHandler>(handler)](
                    std::error_code ec, socket_type socket) mutable {
                    std::shared_ptr<session_type> session{};
                    if (!ec) {
                        session = std::make_shared<session_type>(
                            std::move(socket), self->service_);
                    }
                    handler_(ec, std::move(session));
                });
        }
    };
};

using async_server_unix = async_server<net::local::stream_protocol::acceptor>;
using async_server_tcp = async_server<net::ip::tcp::acceptor>;
using async_server_variant = std::variant<async_server_unix, async_server_tcp>;

class threaded_server {
  private:
    net::thread_pool ctx;
    varlink_service service;
    async_server_variant server;

    auto make_async_server(const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> async_server_variant {
                using Acceptor =
                    typename std::decay_t<decltype(sockaddr)>::protocol_type::acceptor;
                return async_server(Acceptor{ctx, sockaddr}, service);
            },
            endpoint_from_uri(uri));
    }

  public:
    threaded_server(
        const varlink_uri& uri,
        const varlink_service::description& description)
        : ctx(4), service(description), server(make_async_server(uri))
    {
        std::visit(
            [&](auto&& s) { net::post(ctx, [&]() { s.async_serve_forever(); }); },
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
