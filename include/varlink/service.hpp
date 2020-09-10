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
            const auto [ifname, methodname] = message.interfaceAndMethod();
            const auto interface = findInterface(ifname);
            if (interface == interfaces.cend()) {
                return {{"error", "org.varlink.service.InterfaceNotFound"}, {"parameters", {{"interface", ifname}}}};
            }

            const auto sendmore = message.more() ? moreCallback : nullptr;
            const auto reply = interface->call(methodname, message.parameters(), sendmore);

            if (message.oneway()) {
                return nullptr;
            } else if (message.more()) {
                return {{"parameters", reply}, {"continues", false}};
            } else {
                return {{"parameters", reply}};
            }
        }

        void addInterface(const Interface& interface) { interfaces.push_back(interface); }

    };

}

#endif // LIBVARLINK_VARLINK_HPP
