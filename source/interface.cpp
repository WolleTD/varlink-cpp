#include <varlink/detail/scanner.hpp>
#include <varlink/interface.hpp>

namespace varlink {
varlink_interface::varlink_interface(std::string_view description)
{
    auto scanner = detail::scanner(description);
    ifname = scanner.read_interface_name();
    documentation = scanner.get_docstring();
    for (auto member = scanner.read_member(); member.kind != detail::MemberKind::Undefined;
         member = scanner.read_member()) {
        auto names_equal = [&](const auto& v) { return v.name == member.name; };
        if (std::any_of(members.begin(), members.end(), names_equal)) {
            throw std::invalid_argument("Member names must be unique");
        }
        members.push_back(std::move(member));
    }
    if (members.empty()) throw std::invalid_argument("At least one member is required");
}

void varlink_interface::validate( // NOLINT(misc-no-recursion)
    const json& data,
    const detail::type_spec& typespec,
    std::string_view name,
    bool collection) const
{
    auto invalid_parameter = [](std::string_view arg) {
        return varlink_error(
            "org.varlink.service.InvalidParameter", {{"parameter", std::string(arg)}});
    };
    auto is_primitive = [](const json& object, std::string_view type) {
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
        const auto& valtype = typespec.get<detail::string_type>();
        if (not is_primitive(data, valtype)) {
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

std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface)
{
    os << interface.documentation << "interface " << interface.ifname << "\n";
    for (const auto& member : interface.members) {
        os << "\n" << member;
    }
    return os;
}
}
