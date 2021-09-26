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

    [[nodiscard]] const detail::member& find_member(std::string_view name, detail::MemberKind kind) const
    {
        auto i = std::find_if(members.begin(), members.end(), [&](const auto& e) {
            return e.name == name && (e.kind == kind || kind == detail::MemberKind::Undefined);
        });
        if (i == members.end()) throw std::out_of_range(std::string(name));
        return *i;
    }

    [[nodiscard]] bool has_member(std::string_view name, detail::MemberKind kind) const
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
        auto member = scanner.read_member();
        while (member.kind != detail::MemberKind::Undefined) {
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
            member = scanner.read_member();
        }
        if (members.empty()) throw std::invalid_argument("At least one member is required");
        if (not callbacks.empty()) throw std::invalid_argument("Callback for unknown method");
    }

    template <typename ReplyHandler>
    void call(const basic_varlink_message& message, ReplyHandler&& replySender) const
    {
        const auto name = message.interface_and_method().second;
        const auto& m = method(name);
        validate(message.parameters(), m.data.get<detail::vl_struct>()[0].second);
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
             &return_type = m.data.get<detail::vl_struct>()[1].second,
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

    [[nodiscard]] const detail::member& method(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] const detail::member& type(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] const detail::member& error(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Error);
    }

    [[nodiscard]] bool has_method(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] bool has_type(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] bool has_error(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Error);
    }

    void validate(
        const json& data,
        const detail::type_spec& typespec,
        std::string_view name = "<root>",
        bool collection = false) const
    {
        auto invalid_parameter = [](std::string_view arg) {
            return varlink_error(
                "org.varlink.service.InvalidParameter", {{"parameter", std::string(arg)}});
        };
        auto check_type = [](const json& object, std::string_view type) {
            return (type == "string" && object.is_string())
                or (type == "int" && object.is_number_integer())
                or (type == "float" && object.is_number()) or (type == "bool" && object.is_boolean())
                or (type == "object" && !object.is_null());
        };

        if (typespec.is_enum() and data.is_string()) {
            auto& enm = typespec.get<detail::vl_enum>();
            auto s = data.get<std::string>();
            auto matcher = [&s](auto& e) { return e == s; };
            if (not std::any_of(enm.begin(), enm.end(), matcher)) {
                throw invalid_parameter(data.dump());
            }
            return;
        }
        else if (typespec.dict_type and data.is_object()) {
            for (const auto& val : data) {
                validate(val, typespec, name, true);
            }
        }
        else if (typespec.array_type and data.is_array()) {
            for (const auto& val : data) {
                validate(val, typespec, name, true);
            }
        }
        else if (typespec.is_string() and (collection or not(typespec.array_type or typespec.dict_type))) {
            const auto& valtype = typespec.get<std::string>();
            if (not check_type(data, valtype)) {
                try {
                    validate(data, type(valtype).data, name);
                }
                catch (std::out_of_range&) {
                    throw invalid_parameter(name);
                }
            }
        }
        else if (typespec.is_struct() and data.is_object()) {
            auto& strct = typespec.get<detail::vl_struct>();
            for (const auto& param : strct) {
                name = param.first;
                auto spec = param.second;
                if (not data.contains(name) or data[std::string(name)].is_null()) {
                    if (not spec.maybe_type) throw invalid_parameter(name);
                }
                else {
                    const auto& value = data[std::string(name)];
                    validate(value, spec, name); // throws on error
                }
            }
        }
        else {
            throw invalid_parameter(data.dump());
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
