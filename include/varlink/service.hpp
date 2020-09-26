/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVICE_HPP
#define LIBVARLINK_VARLINK_SERVICE_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#include "varlink/common.hpp"
#include "varlink/interface.hpp"
#include "varlink/message.hpp"

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] const varlink::SendMore& sendmore) -> varlink::json

namespace varlink {

    class Service {
    private:
        std::string serviceVendor;
        std::string serviceProduct;
        std::string serviceVersion;
        std::string serviceUrl;
        std::vector<Interface> interfaces;

        auto findInterface(const std::string& ifname) {
            return std::find_if(interfaces.cbegin(), interfaces.cend(),
                                [&ifname](auto &i) { return (ifname == i.name()); });
        }
    public:
        Service() = default;
        Service(std::string vendor, std::string product, std::string version, std::string url) :
                serviceVendor{std::move(vendor)}, serviceProduct{std::move(product)},
                serviceVersion{std::move(version)}, serviceUrl{std::move(url)} {
            addInterface(Interface{org_varlink_service_description(), {
                    {"GetInfo", [this]VarlinkCallback {
                        json info = {
                                {"vendor", serviceVendor},
                                {"product", serviceProduct},
                                {"version", serviceVersion},
                                {"url", serviceUrl}
                        };
                        info["interfaces"] = json::array();
                        for(const auto& interface : interfaces) {
                            info["interfaces"].push_back(interface.name());
                        }
                        return info;
                    }},
                    {"GetInterfaceDescription", [this]VarlinkCallback {
                        const auto& ifname = parameters["interface"].get<std::string>();

                        if (const auto interface = findInterface(ifname); interface != interfaces.cend()) {
                            std::stringstream ss;
                            ss << *interface;
                            return {{"description", ss.str()}};
                        } else {
                            throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
                        }
                    }}
            }});
        }

        // Template dependency: Interface
        json messageCall(const Message &message, const SendMore& moreCallback) noexcept {
            const auto error = [](const std::string &what, const json &params) -> json {
                assert(params.is_object());
                return {{"error", what}, {"parameters", params}};
            };
            const auto [ifname, methodname] = message.interfaceAndMethod();
            const auto interface = findInterface(ifname);
            if (interface == interfaces.cend()) {
                return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            }

            try {
                const auto &method = interface->method(methodname);
                interface->validate(message.parameters(), method.parameters);
                const auto sendmore = message.more() ? moreCallback : nullptr;
                auto response = method.callback(message.parameters(), sendmore);
                try {
                    interface->validate(response, method.returnValue);
                } catch(varlink_error& e) {
                    std::cout << "Response validation error: " << e.args().dump() << std::endl;
                }
                if (message.oneway()) {
                    return nullptr;
                } else if (message.more()) {
                    return {{"parameters", response}, {"continues", false}};
                } else {
                    return {{"parameters", response}};
                }
            } catch (std::out_of_range& e) {
                return error("org.varlink.service.MethodNotFound", {{"method", ifname + '.' + methodname}});
            } catch (std::bad_function_call& e) {
                return error("org.varlink.service.MethodNotImplemented", {{"method", ifname + '.' + methodname}});
            } catch (varlink_error& e) {
                return error(e.what(), e.args());
            } catch (std::exception& e) {
                return error("org.varlink.service.InternalError", {{"what", e.what()}});
            }

        }

        void addInterface(const Interface& interface) { interfaces.push_back(interface); }

    };

}

#endif // LIBVARLINK_VARLINK_HPP
