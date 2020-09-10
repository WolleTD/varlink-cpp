#include <cerrno>
#include <string>
#include <system_error>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <sstream>
#include "varlink/service.hpp"
#include "org.varlink.service.varlink.cpp.inc"

using namespace varlink;

namespace {
    std::system_error systemErrorFromErrno(const std::string& what) {
        return {std::error_code(errno, std::system_category()), what};
    }
}

ServiceConnection::ServiceConnection(std::string address, const std::function<void()>& listener)
        : socketAddress(std::move(address)) {
    listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        throw systemErrorFromErrno("socket() failed");
    }
    struct sockaddr_un addr{AF_UNIX, ""};
    if (socketAddress.length() + 1 > sizeof(addr.sun_path)) {
        throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
    }
    std::strcpy(addr.sun_path, socketAddress.c_str());
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        throw systemErrorFromErrno("bind() failed");
    }
    if (::listen(listen_fd, 1024) < 0) {
        throw systemErrorFromErrno("listen() failed");
    }
    listeningThread = std::thread(listener);
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
        throw systemErrorFromErrno("accept() failed");
    }
    return client_fd;
}

void ServiceConnection::listen(const std::function<void()>& listener) {
    listeningThread = std::thread(listener);
};

ServiceConnection::~ServiceConnection() {
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    listeningThread.join();
    unlink(socketAddress.c_str());
}

std::string_view varlink::org_varlink_service_description() {
    return org_varlink_service_varlink;
}
