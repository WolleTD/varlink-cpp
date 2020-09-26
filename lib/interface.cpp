#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <algorithm>
#include <cassert>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <iostream>
#include <varlink/varlink.hpp>
#include "org.varlink.service.varlink.hpp"

varlink::Interface::Interface(std::string_view fromDescription, const CallbackMap &callbacks)
        : description(fromDescription) {
    pegtl::string_input parser_in { description, __FUNCTION__ };
    try {
        grammar::ParserState state(callbacks);
        pegtl::parse<grammar::interface, grammar::inserter>(parser_in, *this, state);
        if(!state.global.callbacks.empty()) {
            throw std::invalid_argument("Unknown method " + std::string(state.global.callbacks[0].method));
        }
    }
    catch ( const pegtl::parse_error& e) {
        throw std::invalid_argument(e.what());
    }
}

template<typename T>
inline const T& find_member(const std::vector<T>& list, const std::string &name) {
    auto i = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.name == name; });
    if (i == list.end())
        throw std::out_of_range(name);
    return *i;
}

const varlink::Method &varlink::Interface::method(const std::string &name) const {
    return find_member(methods, name);
}

const varlink::Type &varlink::Interface::type(const std::string &name) const {
    return find_member(types, name);
}

const varlink::Error &varlink::Interface::error(const std::string &name) const {
    return find_member(errors, name);
}

template<typename T>
inline bool has_member(const std::vector<T>& list, const std::string &name) {
    return std::any_of(list.begin(), list.end(), [&name](const T& e){ return e.name == name; });
}

bool varlink::Interface::has_method(const std::string &name) const noexcept {
    return has_member(methods, name);
}

bool varlink::Interface::has_type(const std::string &name) const noexcept {
    return has_member(types, name);
}

bool varlink::Interface::has_error(const std::string &name) const noexcept {
    return has_member(errors, name);
}

void varlink::Interface::validate(const json &data, const json &typespec) const {
    //std::cout << data << "\n" << typespec << "\n";
    for (const auto& param : typespec.items()) {
        const auto& name = param.key();
        const auto& spec = param.value();
        if(!(spec.contains("maybe_type") && spec["maybe_type"].get<bool>())
                && (!data.contains(name) || data[name].is_null())) {
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
                if ((valtype == "string" && value.is_string()) or
                    (valtype == "int" && value.is_number_integer()) or
                    (valtype == "float" && value.is_number()) or
                    (valtype == "bool" && value.is_boolean()) or
                    (valtype == "object" && !value.is_null())) {
                    continue;
                } else {
                    try {
                        validate(value, type(valtype).data);
                        continue;
                    } catch(std::out_of_range&) {
                        throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
                    }
                }
            } else {
                throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", name}});
            }
        }
    }
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Interface &interface) {
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

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Type &type) {
    return os << type.description
            << "type " << type.name << " "
            << element_to_string(type.data) << "\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Error &error) {
    return os << error.description
            << "error " << error.name << " "
            << element_to_string(error.data, -1) << "\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Method &method) {
    return os << method.description << "method " << method.name
        << element_to_string(method.parameters, -1) << " -> "
        << element_to_string(method.returnValue) << "\n";
}

std::string varlink::element_to_string(const json &elem, int indent, size_t depth) {
    if (elem.is_string()) {
        return elem.get<std::string>();
    } else {
        const auto is_multiline = [&]() -> bool {
            if (indent < 0) return false;
            if (elem.is_null() || elem.empty()) return false;
            if (elem.is_object()) {
                if (elem.size() > 2) return true;
                for (const auto &member : elem) {
                    if (member.contains("array_type") && member["array_type"].get<bool>()) return true;
                    if (member["type"].is_object()) return true;
                }
            }
            if(element_to_string(elem, -1, depth).size() > 40) return true;
            return false;
        };
        const bool multiline { is_multiline() };
        const std::string spaces = multiline ? std::string(indent * (depth + 1), ' ') : "";
        const std::string sep = multiline ? ",\n" : ", ";
        std::string s = multiline ? "(\n" : "(";
        bool first = true;
        if(elem.is_array()) {
            for (const auto &member : elem) {
                if (first) first = false;
                else s += sep;
                s += spaces + member.get<std::string>();
            }
        } else {
            for (const auto &member : elem.items()) {
                if (first) first = false;
                else s += sep;
                s += spaces + member.key() + ": ";
                auto type = member.value();
                if (type.contains("maybe_type") && type["maybe_type"].get<bool>()) s += "?";
                if (type.contains("array_type") && type["array_type"].get<bool>()) s += "[]";
                if (type.contains("dict_type") && type["dict_type"].get<bool>()) s+= "[string]";
                s += element_to_string(type["type"], indent, depth + 1);
            }
        }
        s += multiline ? "\n" + std::string(indent * depth, ' ') + ")" : ")";
        return s;
    }
}

std::string_view varlink::org_varlink_service_description() {
    return org_varlink_service_varlink;
}
