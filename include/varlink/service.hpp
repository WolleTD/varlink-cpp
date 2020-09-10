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
#include "varlink/message.hpp"

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] const varlink::SendMore& sendmore) -> varlink::json

namespace varlink {

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
        json dispatch(const Message &message, const SendMore& sendmore) {
            const auto [ifname, methodname] = message.interfaceAndMethod();
            try {
                const auto& interface = interfaces.at(ifname);
                return interface.call(methodname, message.parameters(), sendmore);
            }
            catch (std::out_of_range& e) {
                throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
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
            const auto error = [](const std::string &what, const json &params) -> json {
                assert(params.is_object());
                return {{"error", what}, {"parameters", params}};
            };
            const auto sendmore = [&conn](const json &msg) {
                assert(msg.is_object());
                conn.send({{"parameters", msg},
                           {"continues",  true}});
            };

            for (;;) {
                try {
                    const Message message{conn.receive()};

                    if (message.more()) {
                        const auto reply = dispatch(message, sendmore);
                        conn.send({{"parameters", reply}, {"continues", false}});
                    } else {
                        const auto reply = dispatch(message, nullptr);
                        if (!message.oneway()) {
                            conn.send({{"parameters", reply}});
                        }
                    }
                } catch (varlink_error& e) {
                    conn.send(error(e.what(), e.args()));
                } catch(std::invalid_argument& e) {
                    std::cerr << "Invalid message: " << e.what() << std::endl;
                    break;
                } catch(std::system_error& e) {
                    if (e.code() != std::error_code(0, std::system_category())) {
                        std::cerr << "Terminate connection: " << e.what() << std::endl;
                    }
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
                        return info;
                    }},
                    {"GetInterfaceDescription", [this]VarlinkCallback {
                        const auto& ifname = parameters["interface"].get<std::string>();
                        const auto interface = interfaces.find(ifname);
                        if (interface != interfaces.cend()) {
                            std::stringstream ss;
                            ss << interface->second;
                            return {{"description", ss.str()}};
                        } else {
                            throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
                        }
                    }}
            });
        }

    public:
        BasicService(const std::string& address, std::string vendor, std::string product,
                     std::string version, std::string url)
                     : serviceConnection(std::make_unique<ListenConnT>(address, [this](){acceptLoop();})),
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
