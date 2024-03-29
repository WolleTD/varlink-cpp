#include <sstream>
#include <catch2/catch_test_macros.hpp>

#include <varlink/interface.hpp>

using namespace varlink;
using namespace varlink::detail;

TEST_CASE("Varlink interface complete")
{
    SECTION("Parse the org.varlink.service interface")
    {
        std::string_view desc =
            R"IF(# The Varlink Service Interface is provided by every varlink service. It
# describes the service and the interfaces it implements.
interface org.varlink.service

# Get a list of all the interfaces a service provides and information
# about the implementation.
method GetInfo() -> (
        vendor: string,
        product: string,
        version: string,
        url: string,
        interfaces: []string
)

# Get the description of an interface that is implemented by this service.
method GetInterfaceDescription(interface: string) -> (description: string)

# The requested interface was not found.
error InterfaceNotFound (interface: string)

# The requested method was not found
error MethodNotFound (method: string)

# The interface defines the requested method, but the service does not
# implement it.
error MethodNotImplemented (method: string)

# One of the passed parameters is invalid.
error InvalidParameter (parameter: string)
)IF";
        varlink_interface interface{desc};
        REQUIRE("org.varlink.service" == interface.name());
        REQUIRE(
            "# The Varlink Service Interface is provided by every varlink "
            "service. It\n"
            "# describes the service and the interfaces it implements.\n"
            == interface.doc());
        REQUIRE(
            "# Get the description of an interface that is implemented by this "
            "service.\n"
            == interface.method("GetInterfaceDescription").description);
        std::stringstream ss;
        ss << interface;
        REQUIRE(R"IF(# The Varlink Service Interface is provided by every varlink service. It
# describes the service and the interfaces it implements.
interface org.varlink.service

# Get a list of all the interfaces a service provides and information
# about the implementation.
method GetInfo() -> (
    vendor: string,
    product: string,
    version: string,
    url: string,
    interfaces: []string
)

# Get the description of an interface that is implemented by this service.
method GetInterfaceDescription(interface: string) -> (description: string)

# The requested interface was not found.
error InterfaceNotFound (interface: string)

# The requested method was not found
error MethodNotFound (method: string)

# The interface defines the requested method, but the service does not
# implement it.
error MethodNotImplemented (method: string)

# One of the passed parameters is invalid.
error InvalidParameter (parameter: string)
)IF" == ss.str());
    }

    SECTION("Parse a complex interface description")
    {
        std::string_view desc = R"IF(interface org.example.complex
type TypeEnum ( a, b, c )
type TypeFoo (
bool: bool,
int: int,
float: float,
string: string,
enum: ( foo, bar, baz ),
type: TypeEnum,
anon: ( foo: bool, bar: int, baz: ( a: int, b: int) )
)
method Foo(a: (b: bool, c: int), foo: TypeFoo) -> (a: (b: bool, c: int), foo: TypeFoo)
error ErrorFoo (a: (b: bool, c: int), foo: TypeFoo)
)IF";
        varlink_interface interface{desc};
        REQUIRE("org.example.complex" == interface.name());
        std::stringstream ss;
        ss << interface;
        REQUIRE(R"IF(interface org.example.complex

type TypeEnum (a, b, c)

type TypeFoo (
    bool: bool,
    int: int,
    float: float,
    string: string,
    enum: (foo, bar, baz),
    type: TypeEnum,
    anon: (
        foo: bool,
        bar: int,
        baz: (a: int, b: int)
    )
)

method Foo(a: (b: bool, c: int), foo: TypeFoo) -> (
    a: (b: bool, c: int),
    foo: TypeFoo
)

error ErrorFoo (a: (b: bool, c: int), foo: TypeFoo)
)IF" == ss.str());
    }

    SECTION("varlink_interface, Duplicate")
    {
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype T()\ntype T()"));
        REQUIRE_THROWS(varlink_interface(
            "interface org.test\nmethod F()->()\nmethod F()->()"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\nerror E()\nerror E()"));
    }
}

TEST_CASE("Varlink interface header")
{
    SECTION("Parse the interface header with docstring")
    {
        std::string_view desc =
            "# Interface\ninterface org.test\n\nmethod F()->()";
        varlink_interface interface{desc};
        REQUIRE("# Interface\n" == interface.doc());
        REQUIRE("org.test" == interface.name());
        REQUIRE(interface.method("F").description.empty());
    }

    SECTION("Parse the interface header with missing empty line")
    {
        std::string_view desc =
            "# Interface\ninterface org.test\nmethod F()->()";
        varlink_interface interface{desc};
        REQUIRE("# Interface\n" == interface.doc());
        REQUIRE("org.test" == interface.name());
        REQUIRE(interface.method("F").description.empty());
    }

    SECTION("Parse the interface header without a docstring")
    {
        std::string_view desc = "interface org.test\nmethod F()->()";
        varlink_interface interface{desc};
        REQUIRE(interface.doc().empty());
        REQUIRE("org.test" == interface.name());
        REQUIRE(interface.method("F").description.empty());
    }

    SECTION("Parse the interface header with method docstring")
    {
        std::string_view desc =
            "# Interface\ninterface org.test\n# a method\nmethod F()->()";
        varlink_interface interface{desc};
        REQUIRE("# Interface\n" == interface.doc());
        REQUIRE("org.test" == interface.name());
        REQUIRE("# a method\n" == interface.method("F").description);
    }

    SECTION("Parse the interface header with method docstring only")
    {
        std::string_view desc =
            "interface org.test\n# a method\nmethod F()->()";
        varlink_interface interface{desc};
        REQUIRE(interface.doc().empty());
        REQUIRE("org.test" == interface.name());
        REQUIRE("# a method\n" == interface.method("F").description);
    }

    SECTION("Parse valid interface names")
    {
        REQUIRE_NOTHROW(
            varlink_interface("interface org.varlink.service\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface(
            "interface com.example.0example\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface(
            "interface com.example.example-dash\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface(
            "interface xn-lgbbat1ad8j.example.algeria\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface("interface a.b\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface("interface a.b.c\nmethod F()->()"));
        REQUIRE_NOTHROW(
            varlink_interface("interface a1.b1.c1\nmethod F()->()"));
        REQUIRE_NOTHROW(
            varlink_interface("interface a1.b--1.c--1\nmethod F()->()"));
        REQUIRE_NOTHROW(
            varlink_interface("interface a--1.b--1.c--1\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface("interface a.21.c\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface("interface a.1\nmethod F()->()"));
        REQUIRE_NOTHROW(varlink_interface("interface a.0.0\nmethod F()->()"));
    }

    SECTION("Parse invalid interface names")
    {
        REQUIRE_THROWS(varlink_interface(
            "interface com.-example.leadinghyphen\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface(
            "interface com.example-.danglinghyphen-\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface(
            "interface Com.example.uppercase-toplevel\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface(
            "interface Co9.example.number-toplevel\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface(
            "interface 1om.example.number-toplevel\nmethod F()->()"));
        REQUIRE_THROWS(
            varlink_interface("interface com.Example\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface ab\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface .a.b.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.b.c.\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a..b.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface 1.b.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface 8a.0.0\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface -a.b.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.b.c-\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.b-.c-\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.-b.c-\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.-.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.*.c\nmethod F()->()"));
        REQUIRE_THROWS(varlink_interface("interface a.?\nmethod F()->()"));
    }
}

TEST_CASE("Varlink interface methods")
{
    SECTION("At least one member is required")
    {
        REQUIRE_THROWS(varlink_interface("interface org.test\n"));
    }

    SECTION("one method")
    {
        varlink_interface interface("interface org.test\nmethod Test()->()");
        REQUIRE("org.test" == interface.name());
    }

    SECTION("invalid return type")
    {
        REQUIRE_THROWS(
            varlink_interface("interface.org.test\nmethod Test()->(a:)"));
    }
}

TEST_CASE("Varlink interface member access")
{
    SECTION("method member checker")
    {
        varlink_interface interface("interface org.test\nmethod Test()->()");
        REQUIRE(interface.has_method("Test"));
        REQUIRE_FALSE(interface.has_method("Other"));
        REQUIRE_NOTHROW((void)interface.method("Test"));
        REQUIRE_THROWS_AS((void)interface.method("Other"), std::out_of_range);
    }

    SECTION("type member checker")
    {
        varlink_interface interface("interface org.test\ntype T()");
        REQUIRE(interface.has_type("T"));
        REQUIRE_FALSE(interface.has_type("O"));
        REQUIRE_NOTHROW((void)interface.type("T"));
        REQUIRE_THROWS_AS((void)interface.type("O"), std::out_of_range);
    }

    SECTION("error member checker")
    {
        varlink_interface interface("interface org.test\nerror E()");
        REQUIRE(interface.has_error("E"));
        REQUIRE_FALSE(interface.has_error("F"));
        REQUIRE_NOTHROW((void)interface.error("E"));
        REQUIRE_THROWS_AS((void)interface.error("F"), std::out_of_range);
    }
}

template <typename... Args>
type_spec test_spec(Args&&... args)
{
    return type_spec{vl_struct{{"a", type_spec{std::forward<Args>(args)...}}}};
}

TEST_CASE("Varlink interface types")
{
    SECTION("Parse valid type specifications")
    {
        REQUIRE_NOTHROW(varlink_interface("interface org.test\n type I ()"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\n type I (b: bool)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (e: (A, B, C))"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (s: string)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (s: [string]string)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (s: [string]())"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (o: object)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (i: int)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (f: float)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (b: []bool)"));
        REQUIRE_NOTHROW(varlink_interface("interface org.test\ntype I (i: ?int)"));
    }

    SECTION("Parse invalid type specifications")
    {
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: bool[])"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: bool[ ])"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: bool[1])"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: bool[ 1 ])"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: bool[ 1 1 ])"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: [ ]bool)"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: [1]bool)"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: [ 1 ]bool)"));
        REQUIRE_THROWS(
            varlink_interface("interface org.test\ntype I (b: [ 1 1 ]bool)"));
    }

    SECTION("Test enum")
    {
        varlink_interface interface("interface org.test\ntype E(A,B,C)");
        struct Testdata {
            json data;
            type_spec type;
        };
        std::vector<Testdata> testdata{
            {R"({"a":"A"})"_json, test_spec("E")},
            {R"({"a":"B"})"_json, test_spec("E")},
        };
        for (const auto& test : testdata) {
            REQUIRE_NOTHROW(interface.validate(test.data, test.type));
        }
    }

    SECTION("Test valid json objects against varlink typespecs")
    {
        varlink_interface interface("interface org.test\ntype T(n: ?int)");
        struct Testdata {
            json data;
            type_spec type;
        };
        std::vector<Testdata> testdata{
            {R"({"a":0})"_json, test_spec("int")},
            {R"({"a":1337})"_json, test_spec("int")},
            {R"({"a":-123})"_json, test_spec("int")},
            {R"({"a":"a string"})"_json, test_spec("string")},
            {R"({"a":""})"_json, test_spec("string")},
            {R"({"a":true})"_json, test_spec("bool")},
            {R"({"a":false})"_json, test_spec("bool")},
            {R"({"a":145})"_json, test_spec("float")},
            {R"({"a":10.45})"_json, test_spec("float")},
            {R"({"a":-10.45})"_json, test_spec("float")},
            {R"({"a":"object"})"_json, test_spec("object")},
            {R"({"a":null})"_json, test_spec("object", true, false, false)},
            {R"({})"_json, test_spec("int", true, false, false)},
            {R"({"a":[]})"_json, test_spec("int", false, false, true)},
            {R"({"a":[1,2,3]})"_json, test_spec("int", false, false, true)},
            {R"({"a":null})"_json, test_spec("int", true, false, true)},
            {R"({})"_json, test_spec("int", true, false, true)},
            {R"({"a":{"key1":"value1"}})"_json, test_spec("string", false, true, false)},
            {R"({"a":{}})"_json, test_spec("int", false, true, false)},
            {R"({"a":{"b":"string"}})"_json, test_spec(vl_struct{{"b", type_spec{"string"}}})},
            {R"({"a":{"b":"string"}})"_json,
             test_spec(vl_struct{{"b", type_spec{"string", true, false, false}}})},
            {R"({"a":{}})"_json, test_spec(vl_struct{{"b", type_spec{"string", true, false, false}}})},
            {R"({"a":{"n":1}})"_json, test_spec("T")},
            {R"({"a":{}})"_json, test_spec("T")},
            {R"({"a":"one"})"_json, test_spec(vl_enum{"one", "two", "three"})},
            {R"({"a":"three"})"_json, test_spec(vl_enum{"one", "two", "three"})},
            {R"({"a":{"one":{},"two":{},"three":{}}})"_json,
             test_spec(vl_struct{}, false, true, false)},
        };
        for (const auto& test : testdata) {
            REQUIRE_NOTHROW(interface.validate(test.data, test.type));
        }
    }

    SECTION("Test invalid json objects against varlink typespecs")
    {
        varlink_interface interface("interface org.test\ntype T(n: int)");
        struct Testdata {
            json data;
            type_spec type;
        };
        std::vector<Testdata> testdata{
            {R"({"a":"string"})"_json, test_spec("int")},
            {R"({"b":1337})"_json, test_spec("int")},
            {R"({"a":10})"_json, test_spec("string")},
            {R"({"a":0})"_json, test_spec("bool")},
            {R"({"a":"string"})"_json, test_spec("float")},
            {R"({"a":null})"_json, test_spec("object")},
            {R"({"a":null})"_json, test_spec("int")},
            {R"({})"_json, test_spec("int")},
            {R"({"a":[1,2,3]})"_json, test_spec("int")},
            {R"({"a":1})"_json, test_spec("int", false, false, true)},
            {R"({"a":null})"_json, test_spec("int", false, false, true)},
            {R"({})"_json, test_spec("int", false, false, true)},
            {R"({"a":{"key1":"value1"}})"_json, test_spec("string")},
            {R"({"a":[]})"_json, test_spec("int", false, true, false)},
            {R"({"a":{"a":"string"}})"_json, test_spec(vl_struct{{"b", type_spec{"string"}}})},
            {R"({"a":10})"_json, test_spec(vl_struct{{"b", {"string", true, false, false}}})},
            {R"({"a":{}})"_json, test_spec(vl_struct{{"b", type_spec{"string"}}})},
            {R"({"a":{"n":"string"}})"_json, test_spec("T")},
            {R"({"a":{}})"_json, test_spec("T")},
            {R"({"a":"four"})"_json, test_spec(vl_enum{"one", "two", "three"})},
            {R"({"a":{"one":1,"two":2,"three":3}})"_json, test_spec(vl_struct{}, false, true, false)},
        };
        for (const auto& test : testdata) {
            REQUIRE_THROWS_AS(interface.validate(test.data, test.type), invalid_parameter);
        }
    }
}
