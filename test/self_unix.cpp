#include <gtest/gtest.h>

#include <filesystem>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
using std::string;

class Environment : public ::testing::Environment {
    const std::string_view varlink_uri{"unix:test-integration.socket"};
    const varlink_service::description description{"varlink", "test", "1", "test.org"};
    std::unique_ptr<varlink_server> server;

   public:
    void SetUp() override {
        const auto testif =
            "interface org.test\nmethod P(p:string) -> (q:string)\n"
            "method M(n:int,t:?bool)->(m:int)\n";
        std::filesystem::remove("test-integration.socket");
        server = std::make_unique<varlink_server>(varlink_uri, description);
        auto ping_callback = [] varlink_callback { return {{"q", parameters["p"].get<string>()}}; };
        auto more_callback = [] varlink_callback {
            const auto count = parameters["n"].get<int>();
            const bool wait = parameters.contains("t") && parameters["t"].get<bool>();
            for (auto i = 0; i < count; i++) {
                sendmore({{"m", i}});
                if (wait) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {{"m", count}};
        };
        server->add_interface(testif, callback_map{{"P", ping_callback}, {"M", more_callback}});
        server->add_interface("interface org.err\nmethod E() -> ()\n",
                              callback_map{{"E", [] varlink_callback { throw std::exception{}; }}});
    }
};

#define EXPECT_VARLINK_ERROR(statement, error, parameter, value) \
    try {                                                        \
        statement;                                               \
        EXPECT_THROW(statement, varlink_error);                  \
    } catch (varlink_error & e) {                                \
        EXPECT_STREQ(e.what(), error);                           \
        EXPECT_EQ(e.args()[parameter].get<string>(), value);     \
    }

TEST(UnixSocket, GetInfo) {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInfo", {})();
    EXPECT_EQ(resp["vendor"].get<string>(), "varlink");
    EXPECT_EQ(resp["product"].get<string>(), "test");
    EXPECT_EQ(resp["version"].get<string>(), "1");
    EXPECT_EQ(resp["url"].get<string>(), "test.org");
    // resp = client.call("org.varlink.service.GetInfo", {{"value", "should fail"}})();
    // EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InvalidArgument");
    // EXPECT_EQ(resp["parameter"].get<string>(), "value");
}

TEST(UnixSocket, GetInterfaceDescription) {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})();
    EXPECT_EQ(resp["description"].get<string>(),
              "interface org.test\n\n"
              "method P(p: string) -> (q: string)\n\n"
              "method M(n: int, t: ?bool) -> (m: int)\n");
    EXPECT_VARLINK_ERROR(client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.notfound"}})(),
                         "org.varlink.service.InterfaceNotFound", "interface", "org.notfound");
    EXPECT_VARLINK_ERROR(client.call("org.varlink.service.GetInterfaceDescription", {})(),
                         "org.varlink.service.InvalidParameter", "parameter", "interface");
}

TEST(UnixSocket, orgtestPing) {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.test.P", {{"p", "test"}})();
    EXPECT_EQ(resp["q"].get<string>(), "test");
    EXPECT_VARLINK_ERROR(client.call("org.test.P", {{"q", "invalid"}})(),
        "org.varlink.service.InvalidParameter", "parameter", "p");
    EXPECT_VARLINK_ERROR(client.call("org.test.P", {{"p", 20}})(),
        "org.varlink.service.InvalidParameter", "parameter", "p");
}

TEST(UnixSocket, orgtestMore) {
    auto client = varlink_client("unix:test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    EXPECT_EQ(more()["m"].get<int>(), 0);
    EXPECT_EQ(more()["m"].get<int>(), 1);
    EXPECT_EQ(more()["m"].get<int>(), 2);
    EXPECT_EQ(more()["m"].get<int>(), 3);
    EXPECT_EQ(more()["m"].get<int>(), 4);
    EXPECT_EQ(more()["m"].get<int>(), 5);
    EXPECT_EQ(more(), nullptr);
    EXPECT_VARLINK_ERROR(client.call("org.test.M", {{"n", 5}}, callmode::basic)(),
        "org.varlink.service.MethodNotImplemented", "method", "org.test.M");
}

TEST(UnixSocket, orgtestMoreThread) {
    using namespace std::chrono;
    using namespace std::chrono_literals;
    auto client = varlink_client("unix:test-integration.socket");
    auto client2 = varlink_client("unix:test-integration.socket");
    auto begin = steady_clock::now();
    auto more = client.call("org.test.M", {{"n", 1}, {"t", true}}, callmode::more);
    EXPECT_EQ(more()["m"].get<int>(), 0);
    auto begin2 = steady_clock::now();
    auto resp = client2.call("org.test.P", {{"p", "test"}})();
    auto done2 = steady_clock::now();
    EXPECT_EQ(resp["q"].get<string>(), "test");
    EXPECT_EQ(more()["m"].get<int>(), 1);
    EXPECT_EQ(more(), nullptr);
    auto time_to_send = begin2 - begin;
    auto latency = done2 - begin2;
    EXPECT_LT(time_to_send, 1ms);
    EXPECT_LT(latency, 1ms);
}
TEST(UnixSocket, orgtestMoreAndQuit) {
    auto client = varlink_client("unix:test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    EXPECT_EQ(more()["m"].get<int>(), 0);
    EXPECT_EQ(more()["m"].get<int>(), 1);
    EXPECT_EQ(more()["m"].get<int>(), 2);
}

TEST(UnixSocket, orgtestDontread) {
    auto client = varlink_client("unix:test-integration.socket");
    client.call("org.test.P", {{"p", "test"}});
}

TEST(UnixSocket, orgtestOneway) {
    auto client = varlink_client("unix:test-integration.socket");
    auto null = client.call("org.test.P", {{"p", "test"}}, callmode::oneway);
    EXPECT_EQ(null(), nullptr);
}

TEST(UnixSocket, Upgrade) {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.test.P", {{"p", "test"}}, callmode::upgrade)();
    EXPECT_EQ(resp["q"].get<string>(), "test");
}

TEST(UnixSocket, InterfaceNotFound) {
    auto client = varlink_client("unix:test-integration.socket");
    EXPECT_VARLINK_ERROR(client.call("org.notfound.NonExistent", {})(),
        "org.varlink.service.InterfaceNotFound", "interface", "org.notfound");
    EXPECT_VARLINK_ERROR(client.call("org.notfound", {})(),
        "org.varlink.service.InterfaceNotFound", "interface", "org");
    EXPECT_VARLINK_ERROR(client.call("org", {})(),
        "org.varlink.service.InterfaceNotFound", "interface", "org");
}

TEST(UnixSocket, MethodNotFound) {
    auto client = varlink_client("unix:test-integration.socket");
    EXPECT_VARLINK_ERROR(client.call("org.test.NonExistent", {})(),
        "org.varlink.service.MethodNotFound", "method", "org.test.NonExistent");
}

TEST(UnixSocket, InvalidMessage) {
    auto client = json_connection_unix("test-integration.socket");
    client.send(R"({"notmethod":"org.test.NonExistent"})"_json);
    EXPECT_THROW((void)client.receive(), std::system_error);
}

TEST(UnixSocket, InvalidParameterType) {
    auto client = json_connection_unix("test-integration.socket");
    client.send(R"({"method":"org.test.P","parameters":["array"]})"_json);
    EXPECT_THROW((void)client.receive(), std::system_error);
}

TEST(UnixSocket, NotJsonMessage) {
    auto client = socket::unix(socket::mode::connect, "test-integration.socket");
    std::string data = "{totally_not\"a json-message]";
    client.write(data.begin(), data.end() + 1);
    EXPECT_THROW((void)client.read(data.begin(), data.end()), std::system_error);
}

TEST(UnixSocket, Nothing) {
    auto client1 = varlink_client("unix:test-integration.socket");
    auto client2 = varlink_client("unix:test-integration.socket");
    auto client3 = varlink_client("unix:test-integration.socket");
    auto client4 = varlink_client("unix:test-integration.socket");
}

TEST(UnixSocket, InternalException) {
    auto client = varlink_client("unix:test-integration.socket");
    try {
        client.call("org.err.E", {})();
    } catch (varlink_error& e) {
        EXPECT_STREQ(e.what(), "org.varlink.service.InternalError");
    }
}

GTEST_API_ int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    printf("Running main() from %s\n", __FILE__);
    testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new Environment());
    return RUN_ALL_TESTS();
}