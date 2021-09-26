#ifndef LIBVARLINK_MEMBER_HPP
#define LIBVARLINK_MEMBER_HPP

#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace varlink::detail {

enum class MemberKind { Undefined, Type, Error, Method };

struct type_spec;

using vl_enum = std::vector<std::string>;
using vl_struct = std::vector<std::pair<std::string, type_spec>>;
using type_def = std::variant<std::monostate, std::string, vl_enum, vl_struct>;

struct type_spec {
    type_spec() = default;
    explicit type_spec(type_def type_) : type(std::move(type_)) {}
    type_spec(type_def type_, bool maybe, bool dict, bool array)
        : type(std::move(type_)), maybe_type(maybe), dict_type(dict), array_type(array)
    {
    }
    [[nodiscard]] bool is_null() const { return std::holds_alternative<std::monostate>(type); }
    [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(type); }
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
    std::string name;
    std::string description;
    type_spec data;

    member() = default;
    member(MemberKind kind_, std::string_view name_, std::string_view desc, type_spec data_)
        : kind(kind_), name(name_), description(desc), data(std::move(data_))
    {
    }

    explicit operator bool() const { return kind != MemberKind::Undefined; }
};

inline std::string to_string(const type_spec& elem, int indent = 4, size_t depth = 0) // NOLINT(misc-no-recursion)
{
    if (elem.is_string()) { return std::string{elem.get<std::string>()}; }
    else {
        const auto is_multiline = [&]() -> bool {
            if (indent < 0) return false;
            if (elem.is_null() || elem.empty()) return false;
            if (elem.is_struct()) {
                auto& s = elem.get<vl_struct>();
                if (s.size() > 2) return true;
                for (const auto& p : s) {
                    auto& member = p.second;
                    if (member.array_type) return true;
                    if (member.is_struct()) return true;
                }
            }
            return false;
        };
        const bool multiline{is_multiline()};
        const std::string spaces = multiline
                                     ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ')
                                     : "";
        const std::string sep = multiline ? ",\n" : ", ";
        std::string s = multiline ? "(\n" : "(";
        bool first = true;
        if (elem.is_null()) { return "()"; }
        else if (elem.is_enum()) {
            auto& enm = elem.get<vl_enum>();
            for (const auto& value : enm) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + std::string(value);
            }
        }
        else {
            auto& strct = elem.get<vl_struct>();
            for (const auto& member : strct) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + std::string(member.first) + ": ";
                auto& type = member.second;
                if (type.maybe_type) s += "?";
                if (type.array_type) s += "[]";
                if (type.dict_type) s += "[string]";
                s += to_string(type, indent, depth + 1);
            }
        }
        s += multiline ? "\n" + std::string(static_cast<size_t>(indent) * depth, ' ') + ")" : ")";
        return s;
    }
}

inline std::ostream& operator<<(std::ostream& os, const member& mem)
{
    try {
        if (mem.kind == MemberKind::Type) {
            os << mem.description << "type " << mem.name << " " << to_string(mem.data) << "\n";
        }
        else if (mem.kind == MemberKind::Error) {
            os << mem.description << "error " << mem.name << " " << to_string(mem.data, -1) << "\n";
        }
        else if (mem.kind == MemberKind::Method) {
            os << mem.description << "method " << mem.name
               << to_string(mem.data.get<vl_struct>()[0].second, -1) << " -> "
               << to_string(mem.data.get<vl_struct>()[1].second) << "\n";
        }
    }
    catch (...) {
        os << "brokey " << mem.name;
    }
    return os;
}
} // varlink::detail
#endif // LIBVARLINK_MEMBER_HPP
