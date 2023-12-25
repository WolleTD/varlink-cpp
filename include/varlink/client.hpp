/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <utility>
#include <varlink/async_client.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

#define VISIT(X) return std::visit([&](auto&& c) { return c.X; }, *client)

namespace varlink {
struct varlink_client {
    using wait_type = net::socket_base::wait_type;

    explicit varlink_client(net::any_io_executor ex) : ex_(std::move(ex)) {}

    varlink_client(net::any_io_executor ex, const varlink_uri& endpoint) : ex_(std::move(ex))
    {
        connect(endpoint);
    }

    varlink_client(net::any_io_executor ex, std::string_view endpoint)
        : varlink_client(std::move(ex), varlink_uri(endpoint))
    {
    }

    template <
        typename ExecutionContext,
        typename... Args,
        typename = std::enable_if_t<std::is_convertible_v<ExecutionContext&, net::execution_context&>>>
    explicit varlink_client(ExecutionContext& ctx, Args&&... args)
        : varlink_client(ctx.get_executor(), std::forward<Args>(args)...)
    {
    }

    template <typename Socket, typename = std::enable_if_t<std::is_base_of_v<net::socket_base, Socket>>>
    explicit varlink_client(Socket&& socket)
        : ex_(socket.get_executor()), client(async_client(std::forward<Socket>(socket)))
    {
    }

    varlink_client(const varlink_client& src) = delete;
    varlink_client& operator=(const varlink_client&) = delete;
    varlink_client(varlink_client&& src) noexcept = default;
    varlink_client& operator=(varlink_client&& src) = default;

    [[nodiscard]] net::any_io_executor get_executor() const { return ex_; }

    template <typename ConnectHandler>
    auto async_connect(const varlink_uri& endpoint, ConnectHandler&& handler)
    {
        return std::visit(
            [&, handler = std::forward<ConnectHandler>(handler)](auto&& sockaddr) mutable {
                client = client_t<decltype(sockaddr)>(ex_);
                auto& c = std::get<client_t<decltype(sockaddr)>>(*client);
                return c.async_connect(sockaddr, std::forward<ConnectHandler>(handler));
            },
            endpoint_from_uri(endpoint));
    }

    void connect(const varlink_uri& endpoint)
    {
        std::visit(
            [&](auto&& sockaddr) {
                client = client_t<decltype(sockaddr)>(ex_);
                auto& c = std::get<client_t<decltype(sockaddr)>>(*client);
                c.connect(sockaddr);
            },
            endpoint_from_uri(endpoint));
    }

    void connect(const varlink_uri& endpoint, std::error_code& ec)
    {
        std::visit(
            [&](auto&& sockaddr) {
                client = client_t<decltype(sockaddr)>(ex_);
                auto& c = std::get<client_t<decltype(sockaddr)>>(*client);
                c.connect(sockaddr, ec);
            },
            endpoint_from_uri(endpoint));
    }

    void close() { VISIT(close()); }
    void close(std::error_code& ec) { VISIT(close(ec)); }

    void cancel() { VISIT(cancel()); }
    void cancel(std::error_code& ec) { VISIT(cancel(ec)); }

    bool is_open() { VISIT(is_open()); }

    template <typename ReplyHandler>
    auto async_call(const varlink_message& message, ReplyHandler&& handler)
    {
        VISIT(async_call(message, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call(const std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        VISIT(async_call(method, parameters, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_more(const varlink_message_more& message, ReplyHandler&& handler)
    {
        VISIT(async_call_more(message, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_more(const std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        VISIT(async_call_more(method, parameters, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_oneway(const varlink_message_oneway& message, ReplyHandler&& handler)
    {
        VISIT(async_call_oneway(message, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_oneway(const std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        VISIT(async_call_oneway(method, parameters, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_upgrade(const varlink_message_upgrade& message, ReplyHandler&& handler)
    {
        VISIT(async_call_upgrade(message, std::forward<ReplyHandler>(handler)));
    }

    template <typename ReplyHandler>
    auto async_call_upgrade(const std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        VISIT(async_call_upgrade(method, parameters, std::forward<ReplyHandler>(handler)));
    }

    template <typename WaitHandler>
    auto async_wait(wait_type w, WaitHandler&& handler)
    {
        VISIT(async_wait(w, std::forward<WaitHandler>(handler)));
    }

    json call(const varlink_message& message) { VISIT(call(message)); }

    json call(const std::string_view method, const json& parameters)
    {
        VISIT(call(method, parameters));
    }

    std::function<json()> call_more(const varlink_message_more& message)
    {
        VISIT(call_more(message));
    }

    std::function<json()> call_more(const std::string_view method, const json& parameters)
    {
        VISIT(call_more(method, parameters));
    }

    void call_oneway(const varlink_message_oneway& message) { VISIT(call_oneway(message)); }

    void call_oneway(const std::string_view method, const json& parameters)
    {
        VISIT(call_oneway(method, parameters));
    }

    json call_upgrade(const varlink_message_upgrade& message) { VISIT(call_upgrade(message)); }

    json call_upgrade(const std::string_view method, const json& parameters)
    {
        VISIT(call_upgrade(method, parameters));
    }

  private:
    net::any_io_executor ex_;
    std::optional<varlink_client_variant> client;
};
} // namespace varlink

#undef VISIT

#endif
