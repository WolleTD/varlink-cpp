#ifndef LIBVARLINK_INTERFACE_HPP
#define LIBVARLINK_INTERFACE_HPP

#include <varlink/detail/message.hpp>
#include <varlink/detail/scanner.hpp>
#include <varlink/detail/varlink_error.hpp>

#define varlink_callback                               \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] bool wants_more,                 \
     [[maybe_unused]] const varlink::detail::reply_function& send_reply)

namespace varlink {
using detail::callback_function;
using detail::reply_function;

struct user_callback {
    std::string_view name;
    callback_function callback;
};
using callback_map = std::vector<user_callback>;

class varlink_interface {
  private:
    struct method_callback {
        method_callback(std::string method_, callback_function callback_)
            : method(std::move(method_)), callback(std::move(callback_))
        {
        }

        std::string method;
        callback_function callback;
    };
    std::string ifname{};
    std::string documentation{};
    std::string_view description{};

    std::vector<detail::member> members{};
    std::vector<method_callback> methods{};

    [[nodiscard]] const detail::member& find_member(const std::string& name, detail::MemberKind kind) const
    {
        auto i = std::find_if(members.begin(), members.end(), [&](const auto& e) {
            return e.name == name && (e.kind == kind || kind == detail::MemberKind::Undefined);
        });
        if (i == members.end()) throw std::out_of_range(name);
        return *i;
    }

    [[nodiscard]] bool has_member(const std::string& name, detail::MemberKind kind) const
    {
        return std::any_of(members.begin(), members.end(), [&](const auto& e) {
            return e.name == name && (e.kind == kind || kind == detail::MemberKind::Undefined);
        });
    }

  public:
    explicit varlink_interface(std::string_view fromDescription, callback_map callbacks = {})
        : description(fromDescription)
    {
        auto scanner = detail::scanner(std::string(description));
        ifname = scanner.read_interface_name();
        documentation = scanner.get_docstring();
        while (auto member = scanner.read_member()) {
            if (std::any_of(members.begin(), members.end(), [&member](auto& m) {
                    return m.name == member.name;
                })) {
                throw std::invalid_argument("Member names must be unique");
            }
            if (member.kind == detail::MemberKind::Method) {
                auto i = std::find_if(callbacks.begin(), callbacks.end(), [&](const auto& cb) {
                    return cb.name == member.name;
                });
                if (i != callbacks.end()) {
                    methods.emplace_back(member.name, i->callback);
                    callbacks.erase(i);
                }
            }
            members.push_back(std::move(member));
        }
        if (members.empty()) throw std::invalid_argument("At least one member is required");
        if (not callbacks.empty()) throw std::invalid_argument("Callback for unknown method");
    }

    template <typename ReplyHandler>
    void call(const basic_varlink_message& message, ReplyHandler&& replySender) const
    {
        const auto name = message.interface_and_method().second;
        const auto& m = method(name);
        validate(message.parameters(), m.data["parameters"]);
        const auto it = std::find_if(
            methods.begin(), methods.end(), [&name](auto& e) { return e.method == name; });
        if (it == methods.end()) throw std::bad_function_call{};

        it->callback(
            message.parameters(),
            message.more(),
            // This is not an asynchronous callback and exceptions
            // will propagate up to the outer try-catch in this fn.
            // TODO: This isn't true if the callback dispatches async ops
            [this,
             oneway = message.oneway(),
             more = message.more(),
             &return_type = m.data["return_value"],
             replySender = std::forward<ReplyHandler>(replySender)](
                const json::object_t& params, bool continues) mutable {
                validate(params, return_type);

                if (oneway) { replySender(nullptr, false); }
                else if (more) {
                    replySender({{"parameters", params}, {"continues", continues}}, continues);
                }
                else if (continues) { // and not more
                    throw std::bad_function_call{};
                }
                else {
                    replySender({{"parameters", params}}, false);
                }
            });
    }

    [[nodiscard]] const std::string& name() const noexcept { return ifname; }
    [[nodiscard]] const std::string& doc() const noexcept { return documentation; }

    [[nodiscard]] const detail::member& method(const std::string& name) const
    {
        return find_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] const detail::member& type(const std::string& name) const
    {
        return find_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] const detail::member& error(const std::string& name) const
    {
        return find_member(name, detail::MemberKind::Error);
    }

    [[nodiscard]] bool has_method(const std::string& name) const noexcept
    {
        return has_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] bool has_type(const std::string& name) const noexcept
    {
        return has_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] bool has_error(const std::string& name) const noexcept
    {
        return has_member(name, detail::MemberKind::Error);
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

inline std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface)
{
    os << interface.documentation << "interface " << interface.ifname << "\n";
    for (const auto& member : interface.members) {
        os << "\n" << member;
    }
    return os;
}
} // namespace varlink
#endif // LIBVARLINK_INTERFACE_HPP
