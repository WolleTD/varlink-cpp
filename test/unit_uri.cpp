#include <gtest/gtest.h>

#include <varlink/varlink.hpp>

using namespace varlink;
using std::string;

TEST(UriStatic, UnixNoMethod) {
    constexpr std::string_view test = "unix:/tmp/test.sock";
    constexpr auto uri = VarlinkURI(test);
    static_assert(uri.type == VarlinkURI::Type::Unix);
    static_assert(uri.path == "/tmp/test.sock");
    static_assert(uri.qualified_method == "");
    static_assert(uri.interface == "");
    static_assert(uri.method == "");
}

TEST(UriStatic, UnixWithMethod) {
    constexpr std::string_view test = "unix:/tmp/test.sock/org.test.Method";
    constexpr auto uri = VarlinkURI(test, true);
    static_assert(uri.type == VarlinkURI::Type::Unix);
    static_assert(uri.path == "/tmp/test.sock");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST(UriStatic, TcpNoMethod) {
    constexpr std::string_view test = "tcp:127.0.0.1:1337";
    constexpr auto uri = VarlinkURI(test);
    static_assert(uri.type == VarlinkURI::Type::TCP);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "");
    static_assert(uri.interface == "");
    static_assert(uri.method == "");
}

TEST(UriStatic, TcpWithMethod) {
    constexpr std::string_view test = "tcp:127.0.0.1:1337/org.test.Method";
    constexpr auto uri = VarlinkURI(test);
    static_assert(uri.type == VarlinkURI::Type::TCP);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST(UriStatic, RemoveRfu) {
    constexpr std::string_view test = "tcp:127.0.0.1:1337/org.test.Method;rfu=foo";
    constexpr auto uri = VarlinkURI(test);
    static_assert(uri.type == VarlinkURI::Type::TCP);
    static_assert(uri.host == "127.0.0.1");
    static_assert(uri.port == "1337");
    static_assert(uri.qualified_method == "org.test.Method");
    static_assert(uri.interface == "org.test");
    static_assert(uri.method == "Method");
}

TEST(Uri, Invalid) {
    EXPECT_THROW(VarlinkURI("unx:/tmp/test.sock"), std::invalid_argument);
    EXPECT_THROW(VarlinkURI("/tmp/test.sock"), std::invalid_argument);
    EXPECT_THROW(VarlinkURI(""), std::invalid_argument);
    EXPECT_THROW(VarlinkURI("udp:127.0.0.1:123"), std::invalid_argument);
    EXPECT_THROW(VarlinkURI("tcp:127.0.0.1/no.port"), std::invalid_argument);
}

TEST(Uri, Valid) {
    EXPECT_NO_THROW(VarlinkURI("unix:/tmp/test.sock"));
    EXPECT_NO_THROW(VarlinkURI("unix:/tmp/test.sock/org.test.Method", true));
    EXPECT_NO_THROW(VarlinkURI("tcp:127.0.0.1:123"));
    EXPECT_NO_THROW(VarlinkURI("tcp:127.0.0.1:123/org.test.Method"));
    EXPECT_NO_THROW(VarlinkURI("tcp:127.0.0.1:123/org.test.Method;rfu-extension"));
}
