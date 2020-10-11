#include <catch2/catch.hpp>

#include <varlink/varlink.hpp>

using namespace varlink;
using std::string;

TEST_CASE("UriStatic, UnixNoMethod") {
    constexpr std::string_view test = "unix:/tmp/test.sock";
    constexpr auto uri = varlink_uri(test);
    static_assert(uri.type == varlink_uri::type::unix);
    static_assert(uri.path == "/tmp/test.sock");
    static_assert(uri.qualified_method == "");
    static_assert(uri.interface == "");
    static_assert(uri.method == "");
}

TEST_CASE("UriStatic, UnixWithMethod") {
    constexpr std::string_view test = "unix:/tmp/test.sock/org.test.Method";
    constexpr auto uri = varlink_uri(test, true);
    static_assert(uri.type == varlink_uri::type::unix);
    static_assert(uri.path == "/tmp/test.sock");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST_CASE("UriStatic, TcpNoMethod") {
    constexpr std::string_view test = "tcp:127.0.0.1:1337";
    constexpr auto uri = varlink_uri(test);
    static_assert(uri.type == varlink_uri::type::tcp);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "");
    static_assert(uri.interface == "");
    static_assert(uri.method == "");
}

TEST_CASE("UriStatic, TcpWithMethod") {
    constexpr std::string_view test = "tcp:127.0.0.1:1337/org.test.Method";
    constexpr auto uri = varlink_uri(test);
    static_assert(uri.type == varlink_uri::type::tcp);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST_CASE("UriStatic, RemoveRfu") {
    constexpr std::string_view test = "tcp:127.0.0.1:1337/org.test.Method;rfu=foo";
    constexpr auto uri = varlink_uri(test);
    static_assert(uri.type == varlink_uri::type::tcp);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST_CASE("Uri, Invalid") {
    REQUIRE_THROWS_AS(varlink_uri("unx:/tmp/test.sock"), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_uri("/tmp/test.sock"), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_uri(""), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_uri("udp:127.0.0.1:123"), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_uri("tcp:127.0.0.1/no.port"), std::invalid_argument);
}

TEST_CASE("Uri, Valid") {
    REQUIRE_NOTHROW(varlink_uri("unix:/tmp/test.sock"));
    REQUIRE_NOTHROW(varlink_uri("unix:/tmp/test.sock/org.test.Method", true));
    REQUIRE_NOTHROW(varlink_uri("tcp:127.0.0.1:123"));
    REQUIRE_NOTHROW(varlink_uri("tcp:127.0.0.1:123/org.test.Method"));
    REQUIRE_NOTHROW(varlink_uri("tcp:127.0.0.1:123/org.test.Method;rfu-extension"));
}
