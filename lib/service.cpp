#include <cerrno>
#include <string>
#include <system_error>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <varlink.hpp>
#include <sstream>
#include "org.varlink.service.varlink.cpp.inc"

using namespace varlink;

ServiceConnection::ServiceConnection(std::string address, std::function<void(int)> callback)
        : socketAddress(std::move(address)), connectionCallback(std::move(callback)) {
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
        for(;;) {
            try {
                std::thread{connectionCallback, nextClientFd()}.detach();
            } catch (std::system_error& e) {
                if (e.code() == std::errc::invalid_argument) {
                    // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                    break;
                } else {
                    std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
                }
            }
        }
    }};
}

ServiceConnection::ServiceConnection(ServiceConnection &&src) noexcept :
        socketAddress(std::exchange(src.socketAddress, {})),
        listeningThread(std::exchange(src.listeningThread, {})),
        listen_fd(std::exchange(src.listen_fd, -1)) {}

ServiceConnection& ServiceConnection::operator=(ServiceConnection &&rhs) noexcept {
    ServiceConnection s(std::move(rhs));
    std::swap(socketAddress, s.socketAddress);
    std::swap(listeningThread, s.listeningThread);
    std::swap(listen_fd, s.listen_fd);
    return *this;
}

int ServiceConnection::nextClientFd() { //NOLINT (socket changes...)
    auto client_fd = accept(listen_fd, nullptr, nullptr);
    if (client_fd < 0) {
        throw std::system_error { std::error_code(errno, std::system_category()), std::strerror(errno) };
    }
    return client_fd;
}

ServiceConnection::~ServiceConnection() {
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    listeningThread.join();
    unlink(socketAddress.c_str());
}

std::string_view varlink::org_varlink_service_description() {
    return org_varlink_service_varlink;
}
