/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <varlink/async_server.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

namespace varlink {
class varlink_server {
    auto make_async_server(net::io_context& ctx, const varlink_uri& uri, exception_handler ex_handler)
    {
        return std::visit(
            [&](auto&& sockaddr) -> async_server_variant {
                return server_t<decltype(sockaddr)>({ctx, sockaddr}, service, std::move(ex_handler));
            },
            endpoint_from_uri(uri));
    }

  public:
    varlink_server(
        net::io_context& ctx,
        const varlink_uri& uri,
        const varlink_service::description& description,
        exception_handler ex_handler = nullptr)
        : service(description), server(make_async_server(ctx, uri, std::move(ex_handler)))
    {
    }

    varlink_server(
        net::io_context& ctx,
        std::string_view uri,
        const varlink_service::description& description,
        exception_handler ex_handler = nullptr)
        : varlink_server(ctx, varlink_uri(uri), description, std::move(ex_handler))
    {
    }

    varlink_server(const varlink_server& src) = delete;
    varlink_server& operator=(const varlink_server&) = delete;
    varlink_server(varlink_server&& src) noexcept = delete;
    varlink_server& operator=(varlink_server&& src) noexcept = delete;

    void async_serve_forever()
    {
        std::visit([&](auto&& s) { s.async_serve_forever(); }, server);
    }

    void add_interface(varlink_service::interface&& interface)
    {
        service.add_interface(std::move(interface));
    }

    void add_interface(varlink_interface&& spec, callback_map&& callbacks = {})
    {
        service.add_interface(std::move(spec), std::move(callbacks));
    }

    void add_interface(std::string_view definition, callback_map&& callbacks = {})
    {
        service.add_interface(definition, std::move(callbacks));
    }

    auto get_executor()
    {
        return std::visit([](auto&& s) { return s.get_executor(); }, server);
    }

  private:
    varlink_service service;
    async_server_variant server;
};
} // namespace varlink

#endif
