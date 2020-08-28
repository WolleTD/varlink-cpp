#include <tao/pegtl.hpp>
#include <iostream>
#include <iomanip>
#include "interface.hpp"

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
    nlohmann::json& object = interface.state.stack.back();
    object["maybe_type"] = true;
}

INSERTER(grammar::array) {
    nlohmann::json& object = interface.state.stack.back();
    object["array_type"] = true;
}

INSERTER(grammar::dict) {
    nlohmann::json& object = interface.state.stack.back();
    object["dict_type"] = true;
}

INSERTER(grammar::object_start) {
    if (input.position().byte > interface.state.stack.back()["pos"]) {
        interface.state.stack.emplace_back(nlohmann::json{{"fields", nlohmann::json::array()},
                                                          {"pos",    input.position().byte}});
    }
}

INSERTER(grammar::field_name) {
    nlohmann::json& object = interface.state.stack.back();
    if(input.position().byte > object["pos"]) {
        object["fields"].push_back(input.string());
        object["pos"] = input.position().byte;
    }
}

INSERTER(grammar::trivial_element_type) {
    interface.state.stack.back()["last_element_type"] = input.string();
}

INSERTER(grammar::name) {
    if(interface.state.stack.size() == 1)
        interface.state.name = input.string();
}

INSERTER(grammar::venum) {
    auto object = interface.state.stack.back();
    interface.state.stack.pop_back();
    object["last_element_type"] = object["fields"];
}

INSERTER(grammar::argument) {
    auto& object = interface.state.stack.back();
    std::string key = object["fields"].back();
    object[key] = object["last_type"];
    object["last_type"].clear();
    object["fields"].erase(object["fields"].size() - 1);
}

INSERTER(grammar::vstruct) {
    auto object = interface.state.stack.back();
    interface.state.stack.pop_back();
    assert(object["fields"].empty());
    object.erase("fields");
    object.erase("pos");
    object.erase("last_type");
    object.erase("last_element_type");
    object.erase("maybe_type");
    object.erase("dict_type");
    object.erase("array_type");
    interface.state.stack.back()["last_element_type"] = object;
}

INSERTER(grammar::type) {
    auto& object = interface.state.stack.back();
    if(object["last_type"].empty()) {
        object["last_type"]["data"] = object["last_element_type"];
        if(object.contains("dict_type") && object["dict_type"]) {
            object["last_type"]["dict_type"] = true;
            object["dict_type"] = false;
        }
        if(object.contains("array_type") && object["array_type"]) {
            object["last_type"]["array_type"] = true;
            object["array_type"] = false;
        }
        if(object.contains("maybe_type") && object["maybe_type"]) {
            object["last_type"]["maybe_type"] = true;
            object["maybe_type"] = false;
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
    auto& object = interface.state.stack.back();
    object["method_params"] = object["last_element_type"];
}

INSERTER(grammar::interface_name) {
    interface.name = input.string();
    interface.state.stack.emplace_back(nlohmann::json{{"pos", input.position().byte}});
}

INSERTER(grammar::error) {
    auto& object = interface.state.stack.back();
    interface.errors.emplace_back(Error{{interface.state.name, interface.state.docstring, object["last_element_type"]}});
}

INSERTER(grammar::method) {
    auto& object = interface.state.stack.back();
    interface.methods.emplace_back(Method{interface.state.name, interface.state.docstring,
                                          object["method_params"], object["last_element_type"],
                                          nullptr});
}

INSERTER(grammar::vtypedef) {
    auto& object = interface.state.stack.back();
    interface.types.emplace_back(Type{interface.state.name, interface.state.docstring, object["last_element_type"]});
};

varlink::Interface::Interface(std::string fromDescription) : description(std::move(fromDescription)) {
    pegtl::string_input parser_in { description.c_str(), __FUNCTION__ };
    try {
        pegtl::parse<grammar::interface, inserter>(parser_in, *this);
    }
    catch ( const pegtl::parse_error& e) {
        const auto p = e.positions().front();
        std::cerr << e.what() << "\n" << parser_in.line_at(p) << "\n"
            << std::setw(p.column) << '^' << std::endl;
    }
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Interface &interface) {
    os << interface.documentation << "interface " << interface.name << "\n";
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
    os << type.description << "type " << type.name << " (\n";
    bool first = true;
    for(const auto& member : type.data.items()) {
        if (first) first = false;
        else os << ",\n";
        os << "    " << member.key() << ": " << vtype_to_string(member.value());
    }
    return os << "\n)\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Error &error) {
    os << error.description << "error " << error.name << " (";
    bool first = true;
    for(const auto& member : error.data.items()) {
        if (first) first = false;
        else os << ", ";
        os << member.key() << ": " << vtype_to_string(member.value());
    }
    return os << ")\n";
}

std::ostream &varlink::operator<<(std::ostream &os, const varlink::Method &method) {
    return os << method.description << "method " << method.name
        << element_to_string(method.parameters) << " -> "
        << element_to_string(method.returnValue) << "\n";
}

std::string varlink::element_to_string(const nlohmann::json &elem) {
    if (elem.is_string()) {
        return elem.get<std::string>();
    } else if (elem.is_array()){
        bool first = true;
        std::string s = "(";
        for(const auto& item : elem) {
            if (first) first = false;
            else s += ", ";
            s += item.get<std::string>();
        }
        s += ")";
        return s;
    } else if (elem.is_object()) {
        bool first = true;
        std::string s = "(";
        for(const auto& member : elem.items()) {
            if (first) first = false;
            else s += ", ";
            s += member.key() + ": " + vtype_to_string(member.value());
        }
        s += ")";
        return s;
    } else {
        return "null";
    }
}

std::string varlink::vtype_to_string(const nlohmann::json &type) {
    std::string s;
    if (type.contains("maybe_type") && type["maybe_type"]) s += "?";
    if (type.contains("array_type") && type["array_type"]) s += "[]";
    if (type.contains("dict_type") && type["dict_type"]) s+= "[string]";
    s += element_to_string(type["data"]);
    return s;
}
