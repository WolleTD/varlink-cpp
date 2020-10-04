/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <exception>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <utility>
#include <varlink/org.varlink.service.varlink.hpp>
#include <varlink/peg.hpp>
#include <vector>
#undef unix

#define varlink_callback                                                                                            \
    ([[maybe_unused]] const varlink::json& parameters, [[maybe_unused]] const varlink::sendmore_function& sendmore) \
        ->varlink::json

namespace grammar {
template <typename Rule>
struct inserter;
}

namespace varlink {

using nlohmann::json;
using callback_function = std::function<json(const json&, const std::function<void(json)>&)>;
struct method_callback {
    std::string_view method;
    callback_function callback;
};
using callback_map = std::vector<method_callback>;

using sendmore_function = std::function<void(json)>;

class varlink_error : public std::logic_error {
    json _args;

   public:
    varlink_error(const std::string& what, json args) : std::logic_error(what), _args(std::move(args)) {}
    [[nodiscard]] const json& args() const { return _args; }
};

namespace interface {
struct type {
    const std::string name;
    const std::string description;
    const json data;

    type(std::string Name, std::string Description, json Data)
        : name(std::move(Name)), description(std::move(Description)), data(std::move(Data)) {}
    friend std::ostream& operator<<(std::ostream& os, const type& type);
};

struct error : type {
    error(const std::string& Name, const std::string& Description, const json& Data) : type(Name, Description, Data) {}
    friend std::ostream& operator<<(std::ostream& os, const error& error);
};

struct method {
    const std::string name;
    const std::string description;
    const json parameters;
    const json return_value;
    const callback_function callback;

    method(std::string Name, std::string Description, json parameterType, json returnType, callback_function Callback)
        : name(std::move(Name)),
          description(std::move(Description)),
          parameters(std::move(parameterType)),
          return_value(std::move(returnType)),
          callback(std::move(Callback)) {}
    friend std::ostream& operator<<(std::ostream& os, const method& method);
};
}  // namespace interface

class varlink_interface {
   private:
    std::string ifname{};
    std::string documentation{};
    std::string_view description{};

    std::vector<interface::type> types{};
    std::vector<interface::method> methods{};
    std::vector<interface::error> errors{};

    template <typename Rule>
    friend struct grammar::inserter;

   public:
    explicit varlink_interface(std::string_view fromDescription, const callback_map& callbacks = {})
        : description(fromDescription) {
        pegtl::string_input parser_in{description, __FUNCTION__};
        try {
            grammar::parser_state state(callbacks);
            pegtl::parse<grammar::interface, grammar::inserter>(parser_in, *this, state);
            if (!state.global.callbacks.empty()) {
                throw std::invalid_argument("Unknown method " + std::string(state.global.callbacks[0].method));
            }
        } catch (const pegtl::parse_error& e) {
            throw std::invalid_argument(e.what());
        }
    }

    [[nodiscard]] const std::string& name() const noexcept { return ifname; }

    [[nodiscard]] const std::string& doc() const noexcept { return documentation; }

    template <typename T>
    [[nodiscard]] inline const T& find_member(const std::vector<T>& list, const std::string& name) const {
        auto i = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.name == name; });
        if (i == list.end()) throw std::out_of_range(name);
        return *i;
    }

    [[nodiscard]] const interface::method& method(const std::string& name) const { return find_member(methods, name); }

    [[nodiscard]] const interface::type& type(const std::string& name) const { return find_member(types, name); }

    [[nodiscard]] const interface::error& error(const std::string& name) const { return find_member(errors, name); }

    template <typename T>
    [[nodiscard]] inline bool has_member(const std::vector<T>& list, const std::string& name) const {
        return std::any_of(list.begin(), list.end(), [&name](const T& e) { return e.name == name; });
    }

    [[nodiscard]] bool has_method(const std::string& name) const noexcept { return has_member(methods, name); }

    [[nodiscard]] bool has_type(const std::string& name) const noexcept { return has_member(types, name); }

    [[nodiscard]] bool has_error(const std::string& name) const noexcept { return has_member(errors, name); }

    void validate(const json& data, const json& typespec) const {
        // std::cout << data << "\n" << typespec << "\n";
        for (const auto& param : typespec.items()) {
            const auto& name = param.key();
            const auto& spec = param.value();
            if (!(spec.contains("maybe_type") && spec["maybe_type"].get<bool>()) &&
                (!data.contains(name) || data[name].is_null())) {
                throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
            } else if (data.contains(name)) {
                const auto& value = data[name];
                if (((spec.contains("maybe_type") && spec["maybe_type"].get<bool>()) && value.is_null()) or
                    ((spec.contains("dict_type") && spec["dict_type"].get<bool>()) && value.is_object())) {
                    continue;
                } else if (spec.contains("array_type") && spec["array_type"].get<bool>()) {
                    if (value.is_array()) {
                        continue;
                    } else {
                        throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
                    }
                } else if (spec["type"].is_object() && value.is_object()) {
                    validate(value, spec["type"]);
                    continue;
                } else if (spec["type"].is_string()) {
                    const auto& valtype = spec["type"].get<std::string>();
                    if ((valtype == "string" && value.is_string()) or (valtype == "int" && value.is_number_integer()) or
                        (valtype == "float" && value.is_number()) or (valtype == "bool" && value.is_boolean()) or
                        (valtype == "object" && !value.is_null())) {
                        continue;
                    } else {
                        try {
                            validate(value, type(valtype).data);
                            continue;
                        } catch (std::out_of_range&) {
                            throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
                        }
                    }
                } else {
                    throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
                }
            }
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);
};

class varlink_message {
    json json_;
    bool hasmore{false};
    bool hasoneway{false};

   public:
    varlink_message() = default;
    explicit varlink_message(const json& msg) : json_(msg) {
        if (!json_.is_object() || !json_.contains("method") || !json_["method"].is_string()) {
            throw std::invalid_argument("Not a varlink message: " + msg.dump());
        }
        if (!json_.contains("parameters")) {
            json_.emplace("parameters", json::object());
        } else if (!json_["parameters"].is_object()) {
            throw(std::invalid_argument("Not a varlink message: " + msg.dump()));
        }
        hasmore = (msg.contains("more") && msg["more"].get<bool>());
        hasoneway = (msg.contains("oneway") && msg["oneway"].get<bool>());
    }
    [[nodiscard]] bool more() const { return hasmore; }
    [[nodiscard]] bool oneway() const { return hasoneway; }
    [[nodiscard]] const json& parameters() const { return json_["parameters"]; }
    [[nodiscard]] std::pair<std::string, std::string> interface_and_method() const {
        const auto& fqmethod = json_["method"].get<std::string>();
        const auto dot = fqmethod.rfind('.');
        // When there is no dot at all, both fields contain the same value, but it's an invalid
        // interface name anyway
        return {fqmethod.substr(0, dot), fqmethod.substr(dot + 1)};
    }

    friend bool operator==(const varlink_message& lhs, const varlink_message& rhs) noexcept;
};

class varlink_service {
   public:
    struct descr {
        std::string vendor{};
        std::string product{};
        std::string version{};
        std::string url{};
    };

   private:
    descr desc;
    std::vector<varlink_interface> interfaces{};

    auto find_interface(const std::string& ifname) {
        return std::find_if(interfaces.begin(), interfaces.end(), [&ifname](auto& i) { return (ifname == i.name()); });
    }

   public:
    explicit varlink_service(descr description) : desc(std::move(description)) {
        auto getInfo = [this] varlink_callback {
            json info = {
                {"vendor", desc.vendor}, {"product", desc.product}, {"version", desc.version}, {"url", desc.url}};
            info["interfaces"] = json::array();
            for (const auto& interface : interfaces) {
                info["interfaces"].push_back(interface.name());
            }
            return info;
        };
        auto getInterfaceDescription = [this] varlink_callback {
            const auto& ifname = parameters["interface"].get<std::string>();

            if (const auto interface = find_interface(ifname); interface != interfaces.cend()) {
                std::stringstream ss;
                ss << *interface;
                return {{"description", ss.str()}};
            } else {
                throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            }
        };
        set_interface(org_varlink_service_varlink,
                      {{"GetInfo", getInfo}, {"GetInterfaceDescription", getInterfaceDescription}});
    }

    varlink_service(std::string vendor, std::string product, std::string version, std::string url)
        : varlink_service(descr{std::move(vendor), std::move(product), std::move(version), std::move(url)}) {}

    varlink_service(const varlink_service& src) = delete;
    varlink_service& operator=(const varlink_service&) = delete;
    varlink_service(varlink_service&& src) = delete;
    varlink_service& operator=(varlink_service&&) = delete;

    // Template dependency: Interface
    json message_call(const varlink_message& message, const sendmore_function& moreSender) noexcept {
        const sendmore_function sendmore = [&moreSender](const json& msg) {
            assert(msg.is_object());
            moreSender({{"parameters", msg}, {"continues", true}});
        };

        const auto error = [](const std::string& what, const json& params) -> json {
            assert(params.is_object());
            return {{"error", what}, {"parameters", params}};
        };
        const auto [ifname, methodname] = message.interface_and_method();
        const auto interface = find_interface(ifname);
        if (interface == interfaces.cend()) {
            return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
        }

        try {
            const auto& method = interface->method(methodname);
            interface->validate(message.parameters(), method.parameters);
            auto reply_params = method.callback(message.parameters(), (message.more() ? sendmore : nullptr));
            assert(reply_params.is_object());
            try {
                interface->validate(reply_params, method.return_value);
            } catch (varlink_error& e) {
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

    void set_interface(varlink_interface&& interface, bool allowReplace = false) {
        if (auto pos = find_interface(interface.name()); pos == interfaces.end()) {
            interfaces.push_back(std::move(interface));
        } else if (allowReplace) {
            *pos = std::move(interface);
        } else {
            throw std::invalid_argument("Interface already exists and allowReplace not set!");
        }
    }

    void set_interface(std::string_view definition, const callback_map& callbacks, bool allowReplace = false) {
        set_interface(varlink_interface(definition, callbacks), allowReplace);
    }
};

struct varlink_uri {
    enum class type { unix, tcp };
    type type{};
    std::string_view host{};
    std::string_view port{};
    std::string_view path{};
    std::string_view qualified_method{};
    std::string_view interface{};
    std::string_view method{};

    constexpr explicit varlink_uri(std::string_view uri, bool has_interface = false) {
        uri = uri.substr(0, uri.find(';'));
        if (has_interface or (uri.find("tcp:") == 0)) {
            const auto end_of_host = uri.rfind('/');
            path = uri.substr(0, end_of_host);
            if (end_of_host != std::string_view::npos) {
                qualified_method = uri.substr(end_of_host + 1);
                const auto end_of_if = qualified_method.rfind('.');
                interface = qualified_method.substr(0, end_of_if);
                method = qualified_method.substr(end_of_if + 1);
            }
        } else {
            path = uri;
        }
        if (uri.find("unix:") == 0) {
            type = type::unix;
            path = path.substr(5);
        } else if (uri.find("tcp:") == 0) {
            type = type::tcp;
            path = path.substr(4);
            const auto colon = path.find(':');
            if (colon == std::string_view::npos) {
                throw std::invalid_argument("Missing port");
            }
            host = path.substr(0, colon);
            port = path.substr(colon + 1);
        } else {
            throw std::invalid_argument("Unknown protocol / bad URI");
        }
    }
};

inline bool operator==(const varlink_message& lhs, const varlink_message& rhs) noexcept {
    return (lhs.json_ == rhs.json_);
}

namespace interface {
inline std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0) {
    if (elem.is_string()) {
        return elem.get<std::string>();
    } else {
        const auto is_multiline = [&]() -> bool {
            if (indent < 0) return false;
            if (elem.is_null() || elem.empty()) return false;
            if (elem.is_object()) {
                if (elem.size() > 2) return true;
                for (const auto& member : elem) {
                    if (member.contains("array_type") && member["array_type"].get<bool>()) return true;
                    if (member["type"].is_object()) return true;
                }
            }
            if (element_to_string(elem, -1, depth).size() > 40) return true;
            return false;
        };
        const bool multiline{is_multiline()};
        const std::string spaces = multiline ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ') : "";
        const std::string sep = multiline ? ",\n" : ", ";
        std::string s = multiline ? "(\n" : "(";
        bool first = true;
        if (elem.is_array()) {
            for (const auto& member : elem) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + member.get<std::string>();
            }
        } else {
            for (const auto& member : elem.items()) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + member.key() + ": ";
                auto type = member.value();
                if (type.contains("maybe_type") && type["maybe_type"].get<bool>()) s += "?";
                if (type.contains("array_type") && type["array_type"].get<bool>()) s += "[]";
                if (type.contains("dict_type") && type["dict_type"].get<bool>()) s += "[string]";
                s += element_to_string(type["type"], indent, depth + 1);
            }
        }
        s += multiline ? "\n" + std::string(static_cast<size_t>(indent) * depth, ' ') + ")" : ")";
        return s;
    }
}

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::type& type) {
    return os << type.description << "type " << type.name << " " << element_to_string(type.data) << "\n";
}

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::error& error) {
    return os << error.description << "error " << error.name << " " << element_to_string(error.data, -1) << "\n";
}

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::method& method) {
    return os << method.description << "method " << method.name << element_to_string(method.parameters, -1) << " -> "
              << element_to_string(method.return_value) << "\n";
}
}  // namespace interface

inline std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface) {
    os << interface.documentation << "interface " << interface.ifname << "\n";
    for (const auto& type : interface.types) {
        os << "\n" << type;
    }
    for (const auto& method : interface.methods) {
        os << "\n" << method;
    }
    for (const auto& error : interface.errors) {
        os << "\n" << error;
    }
    return os;
}
}  // namespace varlink

#endif  // LIBVARLINK_VARLINK_HPP
