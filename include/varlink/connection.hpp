/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CONNECTION_HPP
#define LIBVARLINK_VARLINK_CONNECTION_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <ext/stdio_filebuf.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "varlink/common.hpp"

namespace varlink {

    class Connection {
    private:
        int socket_fd { -1 };
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;

        std::istream rstream { &filebuf_in };
        std::ostream wstream { &filebuf_out };
    public:
        // Connect to a service via address
        explicit Connection(const std::string& address);

        // Setup message stream on existing connection
        explicit Connection(int posix_fd);

        Connection(const Connection& src) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& src) noexcept;
        Connection& operator=(Connection&& rhs) noexcept;

        void send(const json& message);

        [[nodiscard]] json receive();
    };
}

#endif // LIBVARLINK_VARLINK_CONNECTION_HPP
