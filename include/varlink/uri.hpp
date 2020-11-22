#ifndef LIBVARLINK_URI_HPP
#define LIBVARLINK_URI_HPP
#include <charconv>
#include <variant>
#include <varlink/detail/config.hpp>
#undef unix

namespace varlink {

struct varlink_uri {
    enum class type { unix, tcp };
    type type{};
    std::string_view host{};
    std::string_view port{};
    std::string_view path{};
    std::string_view qualified_method{};
    std::string_view interface{};
    std::string_view method{};

    constexpr explicit varlink_uri(std::string_view uri, bool has_interface = false)
    {
        uri = uri.substr(0, uri.find(';'));
        if (has_interface or (uri.find("tcp:") == 0)) {
            const auto end_of_host = uri.rfind('/');
            path = uri.substr(0, end_of_host);
            if (end_of_host != std::string_view::npos) {
                qualified_method = uri.substr(end_of_host + 1);
                const auto end_of_if = qualified_method.rfind('.');
                interface = qualified_method.substr(0, end_of_if);
                method = qualified_method.substr(end_of_if + 1);
            }
        }
        else {
            path = uri;
        }
        if (uri.find("unix:") == 0) {
            type = type::unix;
            path = path.substr(5);
        }
        else if (uri.find("tcp:") == 0) {
            type = type::tcp;
            path = path.substr(4);
            const auto colon = path.find(':');
            if (colon == std::string_view::npos) {
                throw std::invalid_argument("Missing port");
            }
            host = path.substr(0, colon);
            port = path.substr(colon + 1);
        }
        else {
            throw std::invalid_argument("Unknown protocol / bad URI");
        }
    }
};

using endpoint_variant =
    std::variant<net::local::stream_protocol::endpoint, net::ip::tcp::endpoint>;

inline endpoint_variant endpoint_from_uri(const varlink_uri& uri)
{
    if (uri.type == varlink_uri::type::unix) {
        return net::local::stream_protocol::endpoint{uri.path};
    }
    else if (uri.type == varlink_uri::type::tcp) {
        uint16_t port{0};
        if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port);
            r.ptr != uri.port.end()) {
            throw std::invalid_argument("Invalid port");
        }
        return net::ip::tcp::endpoint(net::ip::make_address_v4(uri.host), port);
    }
    else {
        throw std::invalid_argument("Unsupported protocol");
    }
}

} // namespace varlink
#endif // LIBVARLINK_URI_HPP
