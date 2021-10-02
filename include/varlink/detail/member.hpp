#ifndef LIBVARLINK_MEMBER_HPP
#define LIBVARLINK_MEMBER_HPP

#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace varlink::detail {

#ifdef VARLINK_USE_STRINGS
using string_type = std::string;
#else
using string_type = std::string_view;
#endif

enum class MemberKind { Undefined, Type, Error, Method };

struct type_spec;

using vl_enum = std::vector<string_type>;
using vl_struct = std::vector<std::pair<string_type, type_spec>>;
using type_def = std::variant<std::monostate, string_type, vl_enum, vl_struct>;

struct type_spec {
    type_spec() = default;
    explicit type_spec(type_def type_) : type(std::move(type_)) {}
    type_spec(type_def type_, bool maybe, bool dict, bool array)
        : type(std::move(type_)), maybe_type(maybe), dict_type(dict), array_type(array)
    {
    }
    [[nodiscard]] bool is_null() const { return std::holds_alternative<std::monostate>(type); }
    [[nodiscard]] bool is_string() const { return std::holds_alternative<string_type>(type); }
    [[nodiscard]] bool is_enum() const { return std::holds_alternative<vl_enum>(type); }
    [[nodiscard]] bool is_struct() const { return std::holds_alternative<vl_struct>(type); }
    [[nodiscard]] bool empty() const
    {
        return std::visit(
            [](auto& t) {
                if constexpr (std::is_same_v<std::decay_t<decltype(t)>, std::monostate>) {
                    return true;
                }
                else {
                    return t.empty();
                }
            },
            type);
    }

    template <typename T>
    [[nodiscard]] const T& get() const
    {
        return std::get<T>(type);
    }
    type_def type;
    bool maybe_type{false};
    bool dict_type{false};
    bool array_type{false};
};

struct member {
    MemberKind kind{MemberKind::Undefined};
    string_type name;
    string_type description;
    type_spec data;

    member() = default;
    member(MemberKind kind_, std::string_view name_, std::string_view desc, type_spec data_)
        : kind(kind_), name(name_), description(desc), data(std::move(data_))
    {
    }

    [[nodiscard]] const type_spec& method_parameter_type() const
    {
        if (kind == MemberKind::Method) { return data.get<detail::vl_struct>()[0].second; }
        else {
            throw std::invalid_argument("Not a method");
        }
    }

    [[nodiscard]] const type_spec& method_return_type() const
    {
        if (kind == MemberKind::Method) { return data.get<detail::vl_struct>()[1].second; }
        else {
            throw std::invalid_argument("Not a method");
        }
    }

    explicit operator bool() const { return kind != MemberKind::Undefined; }
};

std::string to_string(const type_spec& elem, int indent = 4, size_t depth = 0);
std::ostream& operator<<(std::ostream& os, const member& mem);

} // varlink::detail
#endif // LIBVARLINK_MEMBER_HPP
