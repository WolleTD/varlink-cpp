#include <tao/pegtl.hpp>
#include <iostream>
#include <iomanip>
#include <varlink.hpp>

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
}

INSERTER(grammar::kwerror) {
    interface.state.docstring = interface.state.moving_docstring;
}

INSERTER(grammar::kwtype) {
    interface.state.docstring = interface.state.moving_docstring;
}

INSERTER(grammar::kwmethod) {
    interface.state.docstring = interface.state.moving_docstring;
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
    auto& state = interface.stack.back();
    interface.errors.emplace(interface.state.name, Error{{interface.state.name,
                                                          interface.state.docstring, state.last_element_type}});
}

INSERTER(grammar::method) {
    auto& state = interface.stack.back();
    MethodCallback callback { nullptr };
    auto cbit = interface.state.callbacks.find(interface.state.name);
    if (cbit != interface.state.callbacks.end()) {
        callback = cbit->second;
        interface.state.callbacks.erase(cbit);
    }
    interface.methods.emplace(interface.state.name, Method{interface.state.name, interface.state.docstring,
                                          interface.state.method_params, state.last_element_type,
                                          callback});
}

INSERTER(grammar::vtypedef) {
    auto& state = interface.stack.back();
    interface.types.emplace(interface.state.name, Type{interface.state.name,
                                                       interface.state.docstring, state.last_element_type});
}

varlink::Interface::Interface(std::string fromDescription, std::map<std::string, MethodCallback> callbacks)
        : description(std::move(fromDescription)), state({.callbacks = std::move(callbacks)}) {
    pegtl::string_input parser_in { description.c_str(), __FUNCTION__ };
    try {
        pegtl::parse<grammar::interface, inserter>(parser_in, *this);
        if(!state.callbacks.empty()) {
            throw std::invalid_argument("Unknown method");
        }
    }
    catch ( const pegtl::parse_error& e) {
        const auto p = e.positions().front();
        std::cerr << e.what() << "\n" << parser_in.line_at(p) << "\n"
            << std::setw(p.column) << '^' << std::endl;
        throw;
    }
    catch ( const std::invalid_argument& e) {
        const auto method { state.callbacks.begin()->first };
        std::cerr << e.what() << "\n" << method << std::endl;
        throw;
    }
}

const varlink::Method &varlink::Interface::method(const std::string &name) const {
    const auto m = methods.find(name);
    if (m != methods.cend()) {
        return m->second;
    } else {
        throw std::invalid_argument("Invalid method");
    }
}

nlohmann::json varlink::Interface::validate(const json &data, const json &type) const {
    auto error = [](std::string param) -> json { return {{"parameter", param}}; };
    std::cout << type << "\n";
    for (const auto& param : type.items()) {
        if(!(param.value().contains("maybe_type") && param.value()["maybe_type"]) && !data.contains(param.key())) {
            return error(param.key());
        }
    }
    return true;
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Interface &interface) {
    os << interface.documentation << "interface " << interface.ifname << "\n";
    for (const auto& type : interface.types) {
        os << "\n" << type.second;
    }
    for (const auto& method : interface.methods) {
        os << "\n" << method.second;
    }
    for (const auto& error : interface.errors) {
        os << "\n" << error.second;
    }
    return os;
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Type &type) {
    return os << type.description
            << "type " << type.name << " "
            << element_to_string(type.data, 2) << "\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Error &error) {
    return os << error.description
            << "error " << error.name << " "
            << element_to_string(error.data) << "\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Method &method) {
    return os << method.description << "method " << method.name
        << element_to_string(method.parameters) << " -> "
        << element_to_string(method.returnValue) << "\n";
}

std::string varlink::element_to_string(const json &elem, size_t indent) {
    if (elem.is_string()) {
        return elem.get<std::string>();
    } else {
        const std::string spaces(indent, ' ');
        const std::string sep = (indent > 0) ? ",\n" : ", ";
        std::string s = (indent > 0) ? "(\n" : "(";
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
                s += spaces + member.key() + ": " + vtype_to_string(member.value());
            }
        }
        s += (indent > 0) ? "\n)" : ")";
        return s;
    }
}

std::string varlink::vtype_to_string(const json &type) {
    std::string s;
    if (type.contains("maybe_type") && type["maybe_type"]) s += "?";
    if (type.contains("array_type") && type["array_type"]) s += "[]";
    if (type.contains("dict_type") && type["dict_type"]) s+= "[string]";
    s += element_to_string(type["type"]);
    return s;
}
