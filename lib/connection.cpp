#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <cerrno>
#include <ext/stdio_filebuf.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>
#include <varlink/client.hpp>
#include <varlink/varlink.hpp>

using namespace varlink;

namespace {
    std::system_error systemErrorFromErrno(const std::string &what) {
        return {std::error_code(errno, std::system_category()), what};
    }
}

Connection::Connection(const std::string& address) {
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        throw systemErrorFromErrno("socket() failed");
    }
    struct sockaddr_un addr { AF_UNIX , "" };
    if (address.length() + 1 > sizeof(addr.sun_path)) {
        throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
    }
    address.copy(addr.sun_path, address.length());
    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        throw systemErrorFromErrno("connect() failed");
    }
    filebuf_in = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::in);
    filebuf_out = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::out);
}

Connection::Connection(int posix_fd) : socket_fd(posix_fd),
    filebuf_in(__gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::in)),
    filebuf_out(__gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::out)) {}

Connection::Connection(Connection&& src) noexcept :
    socket_fd(std::exchange(src.socket_fd, -1)),
    filebuf_in(std::move(src.filebuf_in)),
    filebuf_out(std::move(src.filebuf_out)) {}

Connection& Connection::operator=(Connection &&rhs) noexcept {
    Connection c(std::move(rhs));
    std::swap(socket_fd, c.socket_fd);
    std::swap(filebuf_in, c.filebuf_in);
    std::swap(filebuf_out, c.filebuf_out);
    return *this;
}

void Connection::send(const json& message) {
    wstream << message << '\0' << std::flush;
    if (!wstream.good())
        throw systemErrorFromErrno("Writing to stream failed");
}

json Connection::receive() {
    try {
        json message;
        rstream >> message;
        if (rstream.get() != '\0') {
            throw std::invalid_argument("Trailing bytes in message");
        }
        return message;
    } catch(json::exception& e) {
        if(rstream.good()) {
            throw std::invalid_argument(e.what());
        } else {
            throw systemErrorFromErrno("Reading from stream failed");
        }
    }
}
