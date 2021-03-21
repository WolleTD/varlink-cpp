#ifndef LIBVARLINK_INTERFACE_HPP
#define LIBVARLINK_INTERFACE_HPP

#include <utility>
#include <varlink/detail/peg.hpp>
#include <varlink/detail/utils.hpp>
#include <varlink/detail/varlink_error.hpp>

namespace grammar {
template <typename Rule>
struct inserter;
}

#define varlink_callback                               \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] bool wants_more,                 \
     [[maybe_unused]] const varlink::reply_function& send_reply)

namespace varlink {
using reply_function = std::function<void(json::object_t, bool)>;
using callback_function = std::function<void(const json&, bool, const reply_function&)>;

struct method_callback {
    method_callback(std::string_view method_name, callback_function callback_)
        : method(method_name), callback(std::move(callback_))
    {
    }
    std::string_view method;
    callback_function callback;
};
using callback_map = std::vector<method_callback>;

namespace interface {
struct type {
    const std::string name;
    const std::string description;
    const json data;

    type(std::string Name, std::string Description, json Data)
        : name(std::move(Name)), description(std::move(Description)), data(std::move(Data))
    {
    }
    friend std::ostream& operator<<(std::ostream& os, const type& type);
};

struct error : type {
    error(const std::string& Name, const std::string& Description, const json& Data)
        : type(Name, Description, Data)
    {
    }
    friend std::ostream& operator<<(std::ostream& os, const error& error);
};

struct method {
    const std::string name;
    const std::string description;
    const json parameters;
    const json return_value;
    const callback_function callback;

    method(
        std::string Name,
        std::string Description,
        json parameterType,
        json returnType,
        callback_function Callback)
        : name(std::move(Name)),
          description(std::move(Description)),
          parameters(std::move(parameterType)),
          return_value(std::move(returnType)),
          callback(std::move(Callback))
    {
    }
    friend std::ostream& operator<<(std::ostream& os, const method& method);
};
} // namespace interface

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

    template <typename T>
    [[nodiscard]] inline const T& find_member(const std::vector<T>& list, const std::string& name) const
    {
        auto i = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.name == name; });
        if (i == list.end()) throw std::out_of_range(name);
        return *i;
    }

    template <typename T>
    [[nodiscard]] inline bool has_member(const std::vector<T>& list, const std::string& name) const
    {
        return std::any_of(list.begin(), list.end(), [&name](const T& e) { return e.name == name; });
    }

    template <typename Tuple, std::size_t... Idx>
    callback_map make_callback_map(Tuple&& tuple, std::index_sequence<Idx...>)
    {
        return callback_map{std::make_from_tuple<method_callback>(std::get<Idx>(tuple))...};
    }

    template <typename... Callbacks>
    callback_map make_callback_map(const std::tuple<Callbacks...>& callbacks)
    {
        static_assert(
            (detail::is_tuple_constructible_v<method_callback, Callbacks> && ...),
            "Invalid arguments");
        return make_callback_map(callbacks, std::index_sequence_for<Callbacks...>{});
    }

  public:
    template <typename... Args>
    explicit varlink_interface(std::string_view fromDescription, Args&&... args)
        : description(fromDescription)
    {
        pegtl::string_input parser_in{description, __FUNCTION__};
        try {
            grammar::parser_state state(make_callback_map(detail::make_tuples<2>(args...)));
            pegtl::parse<grammar::interface, grammar::inserter>(parser_in, *this, state);
            if (!state.global.callbacks.empty()) {
                throw std::invalid_argument(
                    "Unknown method " + std::string(state.global.callbacks[0].method));
            }
        }
        catch (const pegtl::parse_error& e) {
            throw std::invalid_argument(e.what());
        }
    }

    [[nodiscard]] const std::string& name() const noexcept { return ifname; }
    [[nodiscard]] const std::string& doc() const noexcept { return documentation; }

    [[nodiscard]] const interface::method& method(const std::string& name) const
    {
        return find_member(methods, name);
    }
    [[nodiscard]] const interface::type& type(const std::string& name) const
    {
        return find_member(types, name);
    }
    [[nodiscard]] const interface::error& error(const std::string& name) const
    {
        return find_member(errors, name);
    }

    [[nodiscard]] bool has_method(const std::string& name) const noexcept
    {
        return has_member(methods, name);
    }
    [[nodiscard]] bool has_type(const std::string& name) const noexcept
    {
        return has_member(types, name);
    }
    [[nodiscard]] bool has_error(const std::string& name) const noexcept
    {
        return has_member(errors, name);
    }

    void validate(const json& data, const json& typespec) const
    {
        if (typespec.is_array() and data.is_string()
            and (std::find(typespec.begin(), typespec.end(), data.get<std::string>()) != typespec.end())) {
            return;
        }
        else if (not data.is_object()) {
            throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", data.dump()}});
        }
        for (const auto& param : typespec.items()) {
            if (param.key() == "_order") continue;
            const auto& name = param.key();
            const auto& spec = param.value();
            if (!(spec.contains("maybe_type") && spec["maybe_type"].get<bool>())
                && (!data.contains(name) || data[name].is_null())) {
                throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
            }
            else if (data.contains(name)) {
                const auto& value = data[name];
                if (((spec.contains("maybe_type") && spec["maybe_type"].get<bool>())
                     && value.is_null())
                    or ((spec.contains("dict_type") && spec["dict_type"].get<bool>())
                        && value.is_object())) {
                    continue;
                }
                else if (spec.contains("array_type") && spec["array_type"].get<bool>()) {
                    if (value.is_array()) { continue; }
                    else {
                        throw varlink_error(
                            "org.varlink.service.InvalidParameter", {{"parameter", name}});
                    }
                }
                else if (spec["type"].is_array() && value.is_string()) {
                    if (std::find(spec["type"].cbegin(), spec["type"].cend(), value)
                        == spec["type"].cend()) {
                        throw varlink_error(
                            "org.varlink.service.InvalidParameter", {{"parameter", name}});
                    }
                }
                else if (spec["type"].is_object() && value.is_object()) {
                    validate(value, spec["type"]);
                    continue;
                }
                else if (spec["type"].is_string()) {
                    const auto& valtype = spec["type"].get<std::string>();
                    if ((valtype == "string" && value.is_string())
                        or (valtype == "int" && value.is_number_integer())
                        or (valtype == "float" && value.is_number())
                        or (valtype == "bool" && value.is_boolean())
                        or (valtype == "object" && !value.is_null())) {
                        continue;
                    }
                    else {
                        try {
                            validate(value, type(valtype).data);
                            continue;
                        }
                        catch (std::out_of_range&) {
                            throw varlink_error(
                                "org.varlink.service.InvalidParameter", {{"parameter", name}});
                        }
                    }
                }
                else {
                    throw varlink_error(
                        "org.varlink.service.InvalidParameter", {{"parameter", name}});
                }
            }
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);
};

namespace interface {
inline std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0)
{
    if (elem.is_string()) { return elem.get<std::string>(); }
    else {
        const auto is_multiline = [&]() -> bool {
            if (indent < 0) return false;
            if (elem.is_null() || elem.empty()) return false;
            if (elem.is_object()) {
                if (elem["_order"].size() > 2) return true;
                for (const auto& member_name : elem["_order"]) {
                    auto& member = elem[member_name.get<std::string>()];
                    if (member.contains("array_type") && member["array_type"].get<bool>())
                        return true;
                    if (member["type"].is_object()) return true;
                }
            }
            if (element_to_string(elem, -1, depth).size() > 40) return true;
            return false;
        };
        const bool multiline{is_multiline()};
        const std::string spaces = multiline
                                     ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ')
                                     : "";
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
        }
        else {
            for (const auto& member : elem["_order"]) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + member.get<std::string>() + ": ";
                auto type = elem[member.get<std::string>()];
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

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::type& type)
{
    return os << type.description << "type " << type.name << " " << element_to_string(type.data)
              << "\n";
}

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::error& error)
{
    return os << error.description << "error " << error.name << " "
              << element_to_string(error.data, -1) << "\n";
}

inline std::ostream& operator<<(std::ostream& os, const varlink::interface::method& method)
{
    return os << method.description << "method " << method.name
              << element_to_string(method.parameters, -1) << " -> "
              << element_to_string(method.return_value) << "\n";
}
} // namespace interface

inline std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface)
{
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
} // namespace varlink
#endif // LIBVARLINK_INTERFACE_HPP
