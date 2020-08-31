#include <varlink.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <exception>

using namespace varlink;

Connection::Connection(const std::string& address) {
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        throw;
    }
    struct sockaddr_un addr { AF_UNIX , "" };
    if (address.length() + 1 > sizeof(addr.sun_path)) {
        throw;
    }
    std::strcpy(addr.sun_path, address.c_str());
    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        throw;
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
    if (!wstream.good()) throw std::system_error(std::make_error_code(std::errc::broken_pipe));
}

json Connection::receive() {
    try {
        json message;
        rstream >> message;
        if (rstream.get() != '\0') {
            std::perror("parse error");
        }
        return message;
    } catch(json::exception& e) {
        if(rstream.eof()) {
            return nullptr;
        } else {
            std::string input;
            getline(rstream, input, '\0');
            return {{"error", e.what()}, {"input", input}};
        }
    }
}
