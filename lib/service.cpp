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
    addInterface(Interface(std::string(org_varlink_service_varlink),
        {
             {"GetInfo", [this]VarlinkCallback {
                 nlohmann::json info = description;
                 info["interfaces"] = nlohmann::json::array();
                 for(const auto& interface : interfaces) {
                     info["interfaces"].push_back(interface.name());
                 }
                 return reply(info);
             }},
             {"GetInterfaceDescription", [this]VarlinkCallback {
                 if (!message["parameters"].contains("interface"))
                     return error("org.varlink.service.InvalidParameter", {{"parameter", "interface"}});
                 const auto& ifname = message["parameters"]["interface"].get<std::string>();
                 for(const auto& interface : interfaces) {
                     if (interface.name() == ifname) {
                         std::stringstream ss;
                         ss << interface;
                         return reply({{"description", ss.str()}});
                     }
                 }
                 return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
             }}
        }));
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

nlohmann::json Service::handle(const nlohmann::json &message, Connection &connection) {
    if (!message.contains("method")) {
        return nullptr;
    }
    const auto &fqmethod = message["method"].get<std::string>();
    const auto dot = fqmethod.rfind('.');
    if (dot == std::string::npos) {
        return error("org.varlink.service.InterfaceNotFound", {{"interface", fqmethod}});
    }
    const auto ifname = fqmethod.substr(0, dot);
    const auto methodname = fqmethod.substr(dot + 1);

    for (const auto& interface : interfaces) {
        if (interface.name() == ifname) {
            try {
                const auto& method = interface.method(methodname);
                // TODO: Validate parameter types
                return method.callback(message, connection);
            }
            catch (std::invalid_argument& e) {
                return error("org.varlink.service.MethodNotFound", {{"method", methodname}});
            }
            catch (std::bad_function_call& e) {
                return error("org.varlink.service.MethodNotImplemented", {{"method", methodname}});
            }
        }
    }
    return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
}

void Service::dispatchConnections() {
    for(;;) {
        try {
            auto conn = nextClientConnection();
            std::thread{[this](Connection conn) {
                for (;;) {
                    auto message = conn.receive();
                    if (message.is_null()) break;
                    auto reply = handle(message, conn);
                    if (!reply.is_null() && !(message.contains("oneway") && message["oneway"])) {
                        conn.send(reply);
                    }
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
