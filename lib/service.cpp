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
    addInterface(org_varlink_service_varlink,
        {
             {"GetInfo", [this]VarlinkCallback {
                 json info = description;
                 info["interfaces"] = json::array();
                 for(const auto& interface : interfaces) {
                     info["interfaces"].push_back(interface.first);
                 }
                 return reply(info);
             }},
             {"GetInterfaceDescription", [this]VarlinkCallback {
                 if (!message["parameters"].contains("interface"))
                     return error("org.varlink.service.InvalidParameter", {{"parameter", "interface"}});
                 const auto& ifname = message["parameters"]["interface"].get<std::string>();
                 const auto interface = interfaces.find(ifname);
                 if (interface != interfaces.cend()) {
                     std::stringstream ss;
                     ss << interface->second;
                     return reply({{"description", ss.str()}});
                 } else {
                     return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
                 }
             }}
        });
    listeningThread = std::thread { [this]() {
        dispatchConnections();
    }};
}

Service::Service(Service &&src) noexcept :
    socketAddress(std::exchange(src.socketAddress, {})),
    description(std::exchange(src.description, {})),
    listeningThread(std::exchange(src.listeningThread, {})),
    listen_fd(std::exchange(src.listen_fd, -1)) {}

Service& Service::operator=(Service &&rhs) noexcept {
    Service s(std::move(rhs));
    std::swap(socketAddress, s.socketAddress);
    std::swap(description, s.description);
    std::swap(listeningThread, s.listeningThread);
    std::swap(listen_fd, s.listen_fd);
    return *this;
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

json Service::handle(const json &message, Connection &connection) {
    const auto &fqmethod = message["method"].get<std::string>();
    const auto dot = fqmethod.rfind('.');
    if (dot == std::string::npos) {
        return error("org.varlink.service.InterfaceNotFound", {{"interface", fqmethod}});
    }
    const auto ifname = fqmethod.substr(0, dot);
    const auto methodname = fqmethod.substr(dot + 1);

    try {
        const auto& interface = interfaces.at(ifname);
        try {
            const auto &method = interface.method(methodname);
            interface.validate(message["parameters"], method.parameters);
            const bool more = (message.contains("more") && message["more"].get<bool>());
            auto response = method.callback(message, connection, more);
            try {
                interface.validate(response["parameters"], method.returnValue);
            } catch(std::invalid_argument& e) {
                std::cout << "Response validation error: " << e.what() << std::endl;
            }
            return response;
        }
        catch (std::out_of_range& e) {
            return error("org.varlink.service.MethodNotFound", {{"method", methodname}});
        }
        catch (std::invalid_argument& e) {
            return error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
        }
        catch (std::bad_function_call& e) {
            return error("org.varlink.service.MethodNotImplemented", {{"method", methodname}});
        }
    }
    catch (std::out_of_range& e) {
        return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
    }
}

void Service::dispatchConnections() {
    for(;;) {
        try {
            auto conn = nextClientConnection();
            std::thread{[this](Connection conn) {
                for (;;) {
                    try {
                        auto message = conn.receive();
                        if (message.is_null()) break;
                        if (message.contains("method")) {
                            if (!message.contains("parameters")) {
                                message["parameters"] = json::object();
                            }
                            auto reply = handle(message, conn);
                            if (!(message.contains("oneway") && message["oneway"])) {
                                conn.send(reply);
                            }
                        }
                    } catch(std::system_error& e) {
                        std::cerr << "Terminate connection: " << e.what() << std::endl;
                        break;
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

void Service::addInterface(std::string_view interface, std::map<std::string, MethodCallback> callbacks) {
    Interface i(interface, std::move(callbacks));
    interfaces.emplace(i.name(), std::move(i));
}
