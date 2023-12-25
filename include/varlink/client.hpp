/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <utility>
#include <varlink/async_client.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

namespace varlink {
struct varlink_client {
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

    void close()
    {
        std::visit([](auto& c) { c.close(); }, *client);
    }
    void close(std::error_code& ec)
    {
        std::visit([&ec](auto& c) { c.close(ec); }, *client);
    }

    void cancel()
    {
        std::visit([](auto& c) { c.cancel(); }, *client);
    }
    void cancel(std::error_code& ec)
    {
        std::visit([&ec](auto& c) { c.cancel(ec); }, *client);
    }

    bool is_open()
    {
        return std::visit([](auto& c) { return c.is_open(); }, *client);
    }

    template <typename... Args>
    auto async_call(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    auto async_call_more(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_more(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    auto async_call_oneway(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_oneway(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    auto async_call_upgrade(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_upgrade(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    auto async_wait(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_wait(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    json call(Args&&... args)
    {
        return std::visit([&](auto&& c) { return c.call(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    std::function<json()> call_more(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.call_more(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    void call_oneway(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.call_oneway(std::forward<Args>(args)...); }, *client);
    }

    template <typename... Args>
    json call_upgrade(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.call_upgrade(std::forward<Args>(args)...); }, *client);
    }

  private:
    net::any_io_executor ex_;
    std::optional<varlink_client_variant> client;
};
} // namespace varlink

#endif
