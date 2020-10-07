#include <gtest/gtest.h>

#include <string_view>
#include <varlink/varlink.hpp>

using namespace varlink;
using namespace varlink::interface;

TEST(varlink_interface, Standard) {
    std::string_view desc = R"IF(# The Varlink Service Interface is provided by every varlink service. It
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
    EXPECT_EQ("org.varlink.service", interface.name());
    EXPECT_EQ(
        "# The Varlink Service Interface is provided by every varlink service. It\n"
        "# describes the service and the interfaces it implements.\n",
        interface.doc());
    EXPECT_EQ("# Get the description of an interface that is implemented by this service.\n",
              interface.method("GetInterfaceDescription").description);
    std::stringstream ss;
    ss << interface;
    EXPECT_EQ(R"IF(# The Varlink Service Interface is provided by every varlink service. It
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
)IF",
              ss.str());
}

TEST(varlink_interface, Complex) {
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
    EXPECT_EQ("org.example.complex", interface.name());
    std::stringstream ss;
    ss << interface;
    EXPECT_EQ(R"IF(interface org.example.complex

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
)IF",
              ss.str());
}

TEST(varlink_interface, Docstring) {
    std::string_view desc = "# Interface\ninterface org.test\n\nmethod F()->()";
    varlink_interface interface{desc};
    EXPECT_EQ("# Interface\n", interface.doc());
    EXPECT_EQ("org.test", interface.name());
    EXPECT_EQ("", interface.method("F").description);
}

TEST(varlink_interface, DocstringNoNewline) {
    std::string_view desc = "# Interface\ninterface org.test\nmethod F()->()";
    varlink_interface interface{desc};
    EXPECT_EQ("# Interface\n", interface.doc());
    EXPECT_EQ("org.test", interface.name());
    EXPECT_EQ("", interface.method("F").description);
}

TEST(varlink_interface, NoMethod) { EXPECT_ANY_THROW(varlink_interface("interface org.test\n")); }

TEST(varlink_interface, OneMethod) {
    varlink_interface interface("interface org.test\nmethod Test()->()");
    EXPECT_EQ("org.test", interface.name());
}

TEST(varlink_interface, OneMethodNoType) { EXPECT_ANY_THROW(varlink_interface("interface.org.test\nmethod Test()->(a:)")); }

TEST(varlink_interface, DomainNames) {
    EXPECT_NO_THROW(varlink_interface("interface org.varlink.service\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface com.example.0example\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface com.example.example-dash\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface com.-example.leadinghyphen\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface xn-lgbbat1ad8j.example.algeria\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface com.example-.danglinghyphen-\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface Com.example.uppercase-toplevel\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface Co9.example.number-toplevel\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface 1om.example.number-toplevel\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface com.Example\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a.b\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a.b.c\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a1.b1.c1\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a1.b--1.c--1\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a--1.b--1.c--1\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a.21.c\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a.1\nmethod F()->()"));
    EXPECT_NO_THROW(varlink_interface("interface a.0.0\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface ab\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface .a.b.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.b.c.\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a..b.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface 1.b.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface 8a.0.0\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface -a.b.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.b.c-\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.b-.c-\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.-b.c-\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.-.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.*.c\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface a.?\nmethod F()->()"));
}

TEST(varlink_interface, Types) {
    EXPECT_NO_THROW(varlink_interface("interface org.test\n type I ()"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\n type I (b: bool)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (e: (A, B, C))"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (s: string)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (s: [string]string)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (s: [string]())"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (o: object)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (i: int)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (f: float)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (b: []bool)"));
    EXPECT_NO_THROW(varlink_interface("interface org.test\ntype I (i: ?int)"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: bool[])"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: bool[ ])"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: bool[1])"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: bool[ 1 ])"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: bool[ 1 1 ])"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: [ ]bool)"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: [1]bool)"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: [ 1 ]bool)"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype I (b: [ 1 1 ]bool)"));
}

TEST(varlink_interface, Duplicate) {
    EXPECT_ANY_THROW(varlink_interface("interface org.test\ntype T()\ntype T()"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\nmethod F()->()\nmethod F()->()"));
    EXPECT_ANY_THROW(varlink_interface("interface org.test\nerror E()\nerror E()"));
}

TEST(varlink_interface, AddCallback) {
    EXPECT_NO_THROW(varlink_interface("interface org.test\nmethod Test()->()", {{"Test", nullptr}}));
}

TEST(varlink_interface, UnknownMethod) {
    EXPECT_THROW(varlink_interface("interface org.test\nmethod Test()->()", {{"Wrong", nullptr}}), std::invalid_argument);
}

TEST(varlink_interface, MethodAccess) {
    varlink_interface interface("interface org.test\nmethod Test()->()");
    EXPECT_TRUE(interface.has_method("Test"));
    EXPECT_FALSE(interface.has_method("Other"));
    EXPECT_NO_THROW((void)interface.method("Test"));
    EXPECT_THROW((void)interface.method("Other"), std::out_of_range);
}

TEST(varlink_interface, TypeAccess) {
    varlink_interface interface("interface org.test\ntype T()");
    EXPECT_TRUE(interface.has_type("T"));
    EXPECT_FALSE(interface.has_type("O"));
    EXPECT_NO_THROW((void)interface.type("T"));
    EXPECT_THROW((void)interface.type("O"), std::out_of_range);
}

TEST(varlink_interface, ErrorAccess) {
    varlink_interface interface("interface org.test\nerror E()");
    EXPECT_TRUE(interface.has_error("E"));
    EXPECT_FALSE(interface.has_error("F"));
    EXPECT_NO_THROW((void)interface.error("E"));
    EXPECT_THROW((void)interface.error("F"), std::out_of_range);
}

TEST(varlink_interface, Validate1) {
    varlink_interface interface("interface org.test\ntype T(n: ?int)");
    struct Testdata {
        json data;
        json type;
    };
    std::vector<Testdata> testdata{
        {R"({"a":0})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":1337})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":-123})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":"a string"})"_json, R"({"a":{"type":"string"}})"_json},
        {R"({"a":""})"_json, R"({"a":{"type":"string"}})"_json},
        {R"({"a":true})"_json, R"({"a":{"type":"bool"}})"_json},
        {R"({"a":false})"_json, R"({"a":{"type":"bool"}})"_json},
        {R"({"a":145})"_json, R"({"a":{"type":"float"}})"_json},
        {R"({"a":10.45})"_json, R"({"a":{"type":"float"}})"_json},
        {R"({"a":-10.45})"_json, R"({"a":{"type":"float"}})"_json},
        {R"({"a":"object"})"_json, R"({"a":{"type":"object"}})"_json},
        {R"({"a":null})"_json, R"({"a":{"type":"int","maybe_type":true}})"_json},
        {R"({})"_json, R"({"a":{"type":"int","maybe_type":true}})"_json},
        {R"({"a":[]})"_json, R"({"a":{"type":"int","array_type":true}})"_json},
        {R"({"a":[1,2,3]})"_json, R"({"a":{"type":"int","array_type":true}})"_json},
        {R"({"a":null})"_json, R"({"a":{"type":"int","array_type":true,"maybe_type":true}})"_json},
        {R"({})"_json, R"({"a":{"type":"int","maybe_type":true,"array_type":true}})"_json},
        {R"({"a":{"key1":"value1"}})"_json, R"({"a":{"type":"string","dict_type":true}})"_json},
        {R"({"a":{}})"_json, R"({"a":{"type":"int","dict_type":true}})"_json},
        {R"({"a":{"b":"string"}})"_json, R"({"a":{"type":{"b":{"type":"string"}}}})"_json},
        {R"({"a":{"b":"string"}})"_json, R"({"a":{"type":{"b":{"type":"string","maybe_type":true}}}})"_json},
        {R"({"a":{}})"_json, R"({"a":{"type":{"b":{"type":"string","maybe_type":true}}}})"_json},
        {R"({"a":{"n":1}})"_json, R"({"a":{"type":"T"}})"_json},
        {R"({"a":{}})"_json, R"({"a":{"type":"T"}})"_json},
        {R"({"a":"one"})"_json, R"({"a":{"type":["one","two","three"]}})"_json},
        {R"({"a":"three"})"_json, R"({"a":{"type":["one","two","three"]}})"_json},
    };
    for (const auto& test : testdata) {
        EXPECT_NO_THROW(interface.validate(test.data, test.type));
    }
}

TEST(varlink_interface, Validate2) {
    varlink_interface interface("interface org.test\ntype T(n: int)");
    struct Testdata {
        json data;
        json type;
    };
    std::vector<Testdata> testdata{
        {R"({"a":"string"})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"b":1337})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":10})"_json, R"({"a":{"type":"string"}})"_json},
        {R"({"a":0})"_json, R"({"a":{"type":"bool"}})"_json},
        {R"({"a":"string"})"_json, R"({"a":{"type":"float"}})"_json},
        {R"({"a":null})"_json, R"({"a":{"type":"object"}})"_json},
        {R"({"a":null})"_json, R"({"a":{"type":"int","maybe_type":false}})"_json},
        {R"({})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":[1,2,3]})"_json, R"({"a":{"type":"int"}})"_json},
        {R"({"a":1})"_json, R"({"a":{"type":"int","array_type":true}})"_json},
        {R"({"a":null})"_json, R"({"a":{"type":"int","array_type":true,"maybe_type":false}})"_json},
        {R"({})"_json, R"({"a":{"type":"int","array_type":true}})"_json},
        {R"({"a":{"key1":"value1"}})"_json, R"({"a":{"type":"string"}})"_json},
        {R"({"a":[]})"_json, R"({"a":{"type":"int","dict_type":true}})"_json},
        {R"({"a":{"a":"string"}})"_json, R"({"a":{"type":{"b":{"type":"string"}}}})"_json},
        {R"({"a":10})"_json, R"({"a":{"type":{"b":{"type":"string","maybe_type":true}}}})"_json},
        {R"({"a":{}})"_json, R"({"a":{"type":{"b":{"type":"string"}}}})"_json},
        {R"({"a":{"n":"string"}})"_json, R"({"a":{"type":"T"}})"_json},
        {R"({"a":{}})"_json, R"({"a":{"type":"T"}})"_json},
        {R"({"a":"four"})"_json, R"({"a":{"type":["one","two","three"]}})"_json},
    };
    for (const auto& test : testdata) {
        EXPECT_THROW(interface.validate(test.data, test.type), varlink_error);
    }
}
