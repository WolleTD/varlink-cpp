#include <ostream>
#include <varlink/detail/member.hpp>

namespace varlink::detail {
std::string to_string(const type_spec& elem, int indent, size_t depth) // NOLINT(misc-no-recursion)
{
    if (elem.is_string()) { return std::string{elem.get<detail::string_type>()}; }
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

std::ostream& operator<<(std::ostream& os, const member& mem)
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
}
