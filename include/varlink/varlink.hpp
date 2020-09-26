/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <vector>
#include <varlink/peg.hpp>

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] const varlink::SendMore& sendmore) -> varlink::json

namespace grammar {
    template<typename Rule>
    struct inserter;
}

namespace varlink {

    using nlohmann::json;
    using MethodCallback = std::function<json(const json&, const std::function<void(json)>&)>;
    struct CallbackEntry {
        std::string_view method;
        MethodCallback callback;
    };
    using CallbackMap = std::vector<CallbackEntry>;

    using SendMore = std::function<void(json)>;

    class varlink_error : public std::logic_error {
        json _args;
    public:
        varlink_error(const std::string& what, json args) : std::logic_error(what), _args(std::move(args)) {}
        [[nodiscard]] const json& args() const { return _args; }
    };

    struct Type {
        const std::string name;
        const std::string description;
        const json data;

        Type(std::string Name, std::string Description, json Data)
                : name(std::move(Name)), description(std::move(Description)), data(std::move(Data)) {}
        friend std::ostream& operator<<(std::ostream& os, const Type& type);
    };

    struct Error : Type {
        Error(const std::string &Name, const std::string &Description, const json &Data)
            : Type(Name, Description, Data) {}
        friend std::ostream& operator<<(std::ostream& os, const Error& error);
    };

    struct Method {
        const std::string name;
        const std::string description;
        const json parameters;
        const json returnValue;
        const MethodCallback callback;

        Method(std::string Name, std::string Description, json parameterType, json returnType, MethodCallback Callback)
                : name(std::move(Name)), description(std::move(Description)), parameters(std::move(parameterType)),
                returnValue(std::move(returnType)), callback(std::move(Callback)) {}
        friend std::ostream& operator<<(std::ostream& os, const Method& method);
    };

    class Interface {
    private:
        std::string ifname;
        std::string documentation;
        std::string_view description;

        std::vector<Type> types;
        std::vector<Method> methods;
        std::vector<Error> errors;

        template<typename Rule>
        friend struct grammar::inserter;
        template<typename... Args>
        void add_type(Args&&... args) { types.emplace_back(std::forward<Args>(args)...); }
        template<typename... Args>
        void add_method(Args&&... args) { methods.emplace_back(std::forward<Args>(args)...); }
        template<typename... Args>
        void add_error(Args&&... args) { errors.emplace_back(std::forward<Args>(args)...); }

    public:
        explicit Interface(std::string_view fromDescription,
                           const CallbackMap &callbacks = {});
        [[nodiscard]] const std::string& name() const noexcept { return ifname; }
        [[nodiscard]] const std::string& doc() const noexcept { return documentation; }
        [[nodiscard]] const Method& method(const std::string& name) const;
        [[nodiscard]] const Type& type(const std::string& name) const;
        [[nodiscard]] const Error& error(const std::string& name) const;
        [[nodiscard]] bool has_method(const std::string& name) const noexcept;
        [[nodiscard]] bool has_type(const std::string& name) const noexcept;
        [[nodiscard]] bool has_error(const std::string& name) const noexcept;
        void validate(const json& data, const json& type) const;

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0);
    std::string_view org_varlink_service_description();

    class Message {
        json json_;
        bool hasmore{false};
        bool hasoneway{false};
    public:
        Message() = default;
        explicit Message(const json& msg) : json_(msg) {
            if (!json_.is_object() || !json_.contains("method") || !json_["method"].is_string()) {
                throw std::invalid_argument(msg.dump());
            }
            if (!json_.contains("parameters")) {
                json_.emplace("parameters", json::object());
            } else if(!json_["parameters"].is_object()) {
                throw(std::invalid_argument("Not a varlink message: " + msg.dump()));
            }
            hasmore = (msg.contains("more") && msg["more"].get<bool>());
            hasoneway = (msg.contains("oneway") && msg["oneway"].get<bool>());
        }
        [[nodiscard]] bool more() const { return hasmore; }
        [[nodiscard]] bool oneway() const { return hasoneway; }
        [[nodiscard]] const json& parameters() const { return json_["parameters"]; }
        [[nodiscard]] std::pair<std::string, std::string> interfaceAndMethod() const {
            const auto& fqmethod = json_["method"].get<std::string>();
            const auto dot = fqmethod.rfind('.');
            // When there is no dot at all, both fields contain the same value, but it's an invalid
            // interface name anyway
            return {fqmethod.substr(0, dot), fqmethod.substr(dot + 1)};
        }

    };

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
                auto reply_params = method.callback(message.parameters(), sendmore);
                assert(reply_params.is_object());
                try {
                    interface->validate(reply_params, method.returnValue);
                } catch(varlink_error& e) {
                    std::cout << "Response validation error: " << e.args().dump() << std::endl;
                }
                if (message.oneway()) {
                    return nullptr;
                } else if (message.more()) {
                    return {{"parameters", reply_params}, {"continues", false}};
                } else {
                    return {{"parameters", reply_params}};
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
