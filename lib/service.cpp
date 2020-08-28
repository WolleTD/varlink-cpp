#include <cerrno>
#include <string>
#include <system_error>
#include <thread>
#include <varlink.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace varlink;

Service::Service(std::string address, Description desc)
        : socketAddress(std::move(address)), description(std::move(desc)) {
    listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        throw;
    }
    struct sockaddr_un addr{AF_UNIX, ""};
    if (socketAddress.length() + 1 > sizeof(addr.sun_path)) {
        throw;
    }
    std::strcpy(addr.sun_path, socketAddress.c_str());
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        throw;
    }
    if (listen(listen_fd, 1024) < 0) {
        throw;
    }
    listeningThread = std::thread { [this]() {
        dispatchConnections();
    }};
}

Service::Service(Service &&src) noexcept {
    socketAddress = std::move(src.socketAddress);
    description = std::move(src.description);
    listeningThread = std::move(src.listeningThread);
    std::swap(listen_fd, src.listen_fd);
}

Service::~Service() {
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    listeningThread.join();
    unlink(socketAddress.c_str());
}

Connection Service::nextClientConnection() const {
    auto client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
        throw std::system_error { std::error_code(errno, std::system_category()), std::strerror(errno) };
    }
    return Connection(client_fd);
}

void Service::dispatchConnections() {
    for(;;) {
        try {
            auto conn = nextClientConnection();
            std::thread{[this](Connection conn) {
                for (;;) {
                    auto message = conn.receive();
                    if (message.is_null()) break;
                    std::cout << "Received: " << message.dump() << "this: " << this << std::endl;
                    conn.send(message);
                }
            }, std::move(conn)}.detach();
        } catch (std::system_error& e) {
            if (e.code() == std::errc::invalid_argument) {
                // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                break;
            } else {
                std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
            }
        }
    }
}
