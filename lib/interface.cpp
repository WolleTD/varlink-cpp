#include <tao/pegtl.hpp>
#include <iostream>
#include "varlink/interface.hpp"

namespace pegtl = TAO_PEGTL_NAMESPACE;

namespace grammar {
    using namespace pegtl;

    struct whitespace : one<' ', '\t'> {};
    struct eol_r : sor<one<'\r', '\n'>, string<'\r', '\n'>> {};
    struct comment : seq<one<'#'>, star<not_one<'\r', '\n'>>, eol_r> {};
    struct eol : sor<seq<star<whitespace>, eol_r>, comment> {};
    struct wce : sor<whitespace, comment, eol_r> {};

    struct field_name : seq< alpha, star<opt<one<'_'> >, alnum> > {};
    struct name : seq< range<'A', 'Z'>, star<alnum> > {};
    struct interface_name : seq<range<'a', 'z'>,
            star< star< one<'-'> >, ranges<'a', 'z', '0', '9'> >,
            plus< one<'.'>, ranges<'a', 'z', '0', '9'>, star<
                    star< one<'-'> >, ranges<'a', 'z', '0', '9'> > > > {};
    struct array : string<'[', ']'> {};
    struct dict : string<'[', 's', 't', 'r', 'i', 'n', 'g', ']'> {};
    struct maybe : one<'?'> {};

    struct venum;
    struct vstruct;
    struct trivial_element_type : sor<
        string<'b', 'o', 'o', 'l'>,
        string<'i', 'n', 't'>,
        string<'f', 'l', 'o', 'a', 't'>,
        string<'s', 't', 'r', 'i', 'n', 'g'>,
        string<'o', 'b', 'j', 'e', 'c', 't'>,
        name
        > {};
    struct element_type : sor<
            trivial_element_type,
            venum,
            vstruct
            > {};
    struct type : sor<
            element_type,
            seq<maybe, element_type>,
            seq<array, type>,
            seq<dict, type>,
            seq<maybe, array, type>,
            seq<maybe, dict, type>
            > {};
    struct object_start : one<'('> {};
    struct object_end : one<')'> {};
    struct venum : seq<
            object_start,
            opt< list< seq< star<wce>, field_name, star<wce> >, one<','> > >,
            star<wce>, object_end
            > {};
    struct argument : seq<star<wce>, field_name, star<wce>, one<':'>, star<wce>, type> {};
    struct vstruct : seq< object_start,
            opt<list< seq< argument, star<wce> >, one<','> > >,
            star<wce>, object_end
            > {};
    struct kwtype : string<'t', 'y', 'p', 'e'> {};
    struct vtypedef : sor<
            seq< kwtype, plus<wce>, name, star<wce>, vstruct>,
            seq< kwtype, plus<wce>, name, star<wce>, venum>
            > {};
    struct kwerror : string<'e', 'r', 'r', 'o', 'r'> {};
    struct error : seq< kwerror, plus<wce>, name, star<wce>, vstruct> {};
    struct kwmethod : string<'m', 'e', 't', 'h', 'o', 'd'> {};
    struct kwarrow : string<'-', '>'> {};
    struct method : seq<
            kwmethod, plus<wce>, name, star<wce>,
            vstruct, star<wce>, kwarrow, star<wce>, vstruct
            > {};
    struct member : sor<
            seq< star<wce>, method >,
            seq< star<wce>, vtypedef >,
            seq< star<wce>, error >
            > {};
    struct kwinterface : string<'i', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e'> {};
    struct interface : must<
            star<wce>, kwinterface, plus<wce>,
            interface_name, eol, list<member, eol>, star<wce>
            > {};
}

#define INSERTER(match) template<> \
struct varlink::Interface::inserter<match> \
{ \
    template<typename ParseInput> \
    static void apply(const ParseInput& input, varlink::Interface& interface); \
};                                 \
template<typename ParseInput>      \
void varlink::Interface::inserter<match>::apply(                               \
        [[maybe_unused]] const ParseInput& input,                              \
        [[maybe_unused]] varlink::Interface& interface)


INSERTER(grammar::wce) {
    if (*input.begin() == '#') {
        interface.state.moving_docstring += input.string();
    } else if (*input.begin() == '\n') {
        interface.state.moving_docstring.clear();
    }
}

INSERTER(grammar::maybe) {
    auto& state = interface.stack.back();
    state.maybe_type = true;
}

INSERTER(grammar::array) {
    auto& state = interface.stack.back();
    state.array_type = true;
}

INSERTER(grammar::dict) {
    auto& state = interface.stack.back();
    state.dict_type = true;
}

INSERTER(grammar::object_start) {
    if (input.position().byte > interface.stack.back().pos) {
        interface.stack.emplace_back(State{.pos = input.position().byte});
    }
}

INSERTER(grammar::field_name) {
    auto& state = interface.stack.back();
    if(input.position().byte > state.pos) {
        state.fields.push_back(input.string());
        state.pos = input.position().byte;
    }
}

INSERTER(grammar::trivial_element_type) {
    interface.stack.back().last_element_type = input.string();
}

INSERTER(grammar::name) {
    if(interface.stack.size() == 1)
        interface.state.name = input.string();
}

INSERTER(grammar::venum) {
    auto state = interface.stack.back();
    interface.stack.pop_back();
    interface.stack.back().last_element_type = state.fields;
}

INSERTER(grammar::argument) {
    auto& state = interface.stack.back();
    std::string key = state.fields.back();
    state.work[key] = state.last_type;
    state.last_type.clear();
    state.fields.pop_back();
}

INSERTER(grammar::vstruct) {
    auto state = interface.stack.back();
    interface.stack.pop_back();
    assert(state.fields.empty());
    interface.stack.back().last_element_type = state.work;
}

INSERTER(grammar::type) {
    auto& state = interface.stack.back();
    if(state.last_type.empty()) {
        state.last_type["type"] = state.last_element_type;
        if(state.dict_type) {
            state.last_type["dict_type"] = true;
            state.dict_type = false;
        }
        if(state.array_type) {
            state.last_type["array_type"] = true;
            state.array_type = false;
        }
        if(state.maybe_type) {
            state.last_type["maybe_type"] = true;
            state.maybe_type = false;
        }
    }
}

INSERTER(grammar::kwinterface) {
    interface.documentation = interface.state.moving_docstring;
    interface.state.moving_docstring.clear();
}

INSERTER(grammar::kwerror) {
    interface.state.docstring = interface.state.moving_docstring;
    interface.state.moving_docstring.clear();
}

INSERTER(grammar::kwtype) {
    interface.state.docstring = interface.state.moving_docstring;
    interface.state.moving_docstring.clear();
}

INSERTER(grammar::kwmethod) {
    interface.state.docstring = interface.state.moving_docstring;
    interface.state.moving_docstring.clear();
}

INSERTER(grammar::kwarrow) {
    auto& state = interface.stack.back();
    interface.state.method_params = state.last_element_type;
}

INSERTER(grammar::interface_name) {
    interface.ifname = input.string();
    interface.stack.emplace_back(State{.pos = input.position().byte});
}

INSERTER(grammar::error) {
    for(const auto& m : interface.errors) {
        if (m.name == interface.state.name)
            throw std::invalid_argument("Multiple definition of error " + interface.state.name);
    }
    auto& state = interface.stack.back();
    interface.errors.emplace_back(Error{{interface.state.name,
                                         interface.state.docstring, state.last_element_type}});
}

INSERTER(grammar::method) {
    for(const auto& m : interface.methods) {
        if (m.name == interface.state.name)
            throw std::invalid_argument("Multiple definition of method " + interface.state.name);
    }
    auto& state = interface.stack.back();
    MethodCallback callback { nullptr };
    auto cbit = interface.state.callbacks.find(interface.state.name);
    if (cbit != interface.state.callbacks.end()) {
        callback = cbit->second;
        interface.state.callbacks.erase(cbit);
    }
    interface.methods.emplace_back(Method{interface.state.name, interface.state.docstring,
                                          interface.state.method_params, state.last_element_type,
                                          callback});
}

INSERTER(grammar::vtypedef) {
    for(const auto& m : interface.types) {
        if (m.name == interface.state.name)
            throw std::invalid_argument("Multiple definition of type " + interface.state.name);
    }
    auto& state = interface.stack.back();
    interface.types.emplace_back(Type{interface.state.name,
                                      interface.state.docstring, state.last_element_type});
}

varlink::Interface::Interface(std::string_view fromDescription, CallbackMap callbacks)
        : description(fromDescription), state({.callbacks = std::move(callbacks)}) {
    pegtl::string_input parser_in { description, __FUNCTION__ };
    try {
        pegtl::parse<grammar::interface, inserter>(parser_in, *this);
        if(!state.callbacks.empty()) {
            throw std::invalid_argument("Unknown method " + state.callbacks.begin()->first);
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
            if((spec.contains("maybe_type") && spec["maybe_type"].get<bool>())
                && value.is_null()) {
                continue;
            } else if ((spec.contains("dict_type") && spec["dict_type"].get<bool>())
                    && value.is_object()) {
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
                if (valtype == "string" && value.is_string()) {
                    continue;
                } else if (valtype == "int" && value.is_number_integer()) {
                    continue;
                } else if (valtype == "float" && value.is_number()) {
                    continue;
                } else if (valtype == "bool" && value.is_boolean()) {
                    continue;
                } else if (valtype == "object" && !value.is_null()) {
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
