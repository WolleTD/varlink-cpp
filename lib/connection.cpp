#include <varlink.hpp>

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

void Connection::send(const nlohmann::json& message) {
    wstream << message << '\0' << std::flush;
}

nlohmann::json Connection::receive() {
    try {
        nlohmann::json message;
        rstream >> message;
        if (rstream.get() != '\0') {
            std::perror("parse error");
        }
        return message;
    } catch(...) {
        std::string line;
        getline(rstream, line, '\0');
        if (line.length()) {
            std::cout << "Couldn't parse: " << line << std::endl;
        }
        return {};
    }
}
