/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVICE_HPP
#define LIBVARLINK_VARLINK_SERVICE_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <thread>
#include <utility>

#include "varlink/common.hpp"

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& message, \
     [[maybe_unused]] const std::function<void(varlink::json)>& sendmore, \
     [[maybe_unused]] bool more) -> varlink::json

namespace varlink {

    inline json reply(json params) {
        assert(params.is_object());
        return {{"parameters", std::move(params)}};
    }
    inline json reply_continues(json params, bool continues = true) {
        assert(params.is_object());
        return {{"parameters", std::move(params)}, {"continues", continues}};
    }
    inline json error(std::string what, json params) {
        assert(params.is_object());
        return {{"error", std::move(what)}, {"parameters", std::move(params)}};
    }

    std::string_view org_varlink_service_description();

    class ServiceConnection {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        int listen_fd { -1 };
    public:
        explicit ServiceConnection(std::string address, const std::function<void()>& listener = nullptr);
        ServiceConnection(const ServiceConnection& src) = delete;
        ServiceConnection& operator=(const ServiceConnection&) = delete;
        ServiceConnection(ServiceConnection&& src) noexcept;
        ServiceConnection& operator=(ServiceConnection&& rhs) noexcept;
        ~ServiceConnection();

        [[nodiscard]] int nextClientFd();
        void listen(const std::function<void()>& listener);
    };

    template<typename ListenConnT, typename ClientConnT, typename InterfaceT>
    class BasicService {
    private:
        std::unique_ptr<ListenConnT> serviceConnection;
        std::string serviceVendor;
        std::string serviceProduct;
        std::string serviceVersion;
        std::string serviceUrl;
        std::map<std::string, InterfaceT> interfaces;

        // Template dependency: Interface
        json handle(const json &message, const std::function<void(json)>& sendmore, bool more) {
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
                    auto response = method.callback(message, sendmore, more);
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

        void acceptLoop() {
            for(;;) {
                try {
                    std::thread{[this](int fd) {
                        clientLoop(ClientConnT(fd));
                    }, serviceConnection->nextClientFd()}.detach();
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

        // Template dependency: ClientConnection
        void clientLoop(ClientConnT conn) {
            for (;;) {
                try {
                    json message = conn.receive();
                    if (message.is_null() || !message.contains("method")) break;
                    message.merge_patch(R"({"parameters":{}})"_json);
                    const bool more = (message.contains("more") && message["more"].get<bool>());
                    auto reply = handle(message, [&conn,more](const json& msg){
                        if (more) conn.send(msg);
                        else throw std::invalid_argument{"more"};
                    }, more);
                    if (!(message.contains("oneway") && message["oneway"].get<bool>())) {
                        conn.send(reply);
                    }
                } catch(std::system_error& e) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                    break;
                }
            }
        }

        // Template dependency: Interface
        void addServiceInterface() {
            addInterface(org_varlink_service_description(), {
                    {"GetInfo", [this]VarlinkCallback {
                        json info = {
                                {"vendor", serviceVendor},
                                {"product", serviceProduct},
                                {"version", serviceVersion},
                                {"url", serviceUrl}
                        };
                        info["interfaces"] = json::array();
                        for(const auto& interface : interfaces) {
                            info["interfaces"].push_back(interface.first);
                        }
                        return reply(info);
                    }},
                    {"GetInterfaceDescription", [this]VarlinkCallback {
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
        }

    public:
        BasicService(const std::string& address, std::string vendor, std::string product,
                     std::string version, std::string url)
                     : serviceConnection(std::make_unique<ListenConnT>(address), [this](){acceptLoop();}),
                     serviceVendor{std::move(vendor)}, serviceProduct{std::move(product)},
                     serviceVersion{std::move(version)}, serviceUrl{std::move(url)} {
            addServiceInterface();
        }
        explicit BasicService(std::unique_ptr<ListenConnT> listenConn)
            : serviceConnection(std::move(listenConn)) {
            serviceConnection->listen([this]() { acceptLoop(); });
            addServiceInterface();
        }
        /*BasicService(BasicService &&src) noexcept :
                listeningThread(std::exchange(src.listeningThread, {})) {}

        BasicService& operator=(BasicService &&rhs) noexcept {
            BasicService s(std::move(rhs));
            std::swap(listeningThread, s.listeningThread);
            return *this;
        }*/


        void addInterface(InterfaceT interface) { interfaces.emplace(interface.name(), std::move(interface)); }
        void addInterface(std::string_view interface, const CallbackMap& callbacks) {
            addInterface(InterfaceT(interface, callbacks));
        }
    };
}

#endif // LIBVARLINK_VARLINK_HPP
