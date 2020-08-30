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

Connection::Connection(int posix_fd) : socket_fd(posix_fd) {
    filebuf_in = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::in);
    filebuf_out = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::out);
}

Connection::Connection(Connection&& src) noexcept {
    std::swap(socket_fd, src.socket_fd);
    filebuf_in = std::move(src.filebuf_in);
    filebuf_out = std::move(src.filebuf_out);
}

void Connection::send(const json& message) {
    wstream << message << '\0' << std::flush;
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
