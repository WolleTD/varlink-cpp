#include <gtest/gtest.h>

#include <filesystem>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
using std::string;

class Environment : public ::testing::Environment {
    const std::string_view varlink_uri{"unix:test-integration.socket"};
    const Service::Description description{"varlink", "test", "1", "test.org"};
    std::unique_ptr<VarlinkServer> server;

   public:
    void SetUp() override {
        const auto testif =
            "interface org.test\nmethod P(p:string) -> (q:string)\n"
            "method M(n:int,t:?bool)->(m:int)\n";
        std::filesystem::remove("test-integration.socket");
        server = std::make_unique<VarlinkServer>(varlink_uri, description);
        auto ping_callback = [] VarlinkCallback { return {{"q", parameters["p"].get<string>()}}; };
        auto more_callback = [] VarlinkCallback {
            const auto count = parameters["n"].get<int>();
            const bool wait = parameters.contains("t") && parameters["t"].get<bool>();
            for (auto i = 0; i < count; i++) {
                sendmore({{"m", i}});
                if (wait) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {{"m", count}};
        };
        server->setInterface(testif, CallbackMap{{"P", ping_callback}, {"M", more_callback}});
        server->setInterface("interface org.err\nmethod E() -> ()\n",
                             CallbackMap{{"E", [] VarlinkCallback { throw std::exception{}; }}});
    }
};

TEST(UnixSocket, GetInfo) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInfo", {})();
    EXPECT_EQ(resp["parameters"]["vendor"].get<string>(), "varlink");
    EXPECT_EQ(resp["parameters"]["product"].get<string>(), "test");
    EXPECT_EQ(resp["parameters"]["version"].get<string>(), "1");
    EXPECT_EQ(resp["parameters"]["url"].get<string>(), "test.org");
    // resp = client.call("org.varlink.service.GetInfo", {{"value", "should fail"}})();
    // EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InvalidArgument");
    // EXPECT_EQ(resp["parameters"]["parameter"].get<string>(), "value");
}

TEST(UnixSocket, GetInterfaceDescription) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})();
    EXPECT_EQ(resp["parameters"]["description"].get<string>(),
              "interface org.test\n\n"
              "method P(p: string) -> (q: string)\n\n"
              "method M(n: int, t: ?bool) -> (m: int)\n");
    resp = client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.notfound"}})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(resp["parameters"]["interface"].get<string>(), "org.notfound");
    resp = client.call("org.varlink.service.GetInterfaceDescription", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InvalidParameter");
    EXPECT_EQ(resp["parameters"]["parameter"].get<string>(), "interface");
}

TEST(UnixSocket, orgtestPing) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.test.P", {{"p", "test"}})();
    EXPECT_EQ(resp["parameters"]["q"].get<string>(), "test");
    resp = client.call("org.test.P", {{"q", "invalid"}})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InvalidParameter");
    EXPECT_EQ(resp["parameters"]["parameter"].get<string>(), "p");
    resp = client.call("org.test.P", {{"p", 20}})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InvalidParameter");
    EXPECT_EQ(resp["parameters"]["parameter"].get<string>(), "p");
}

TEST(UnixSocket, orgtestMore) {
    auto client = Client("test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, Client::CallMode::More);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 0);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 1);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 2);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 3);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 4);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 5);
    EXPECT_EQ(more(), nullptr);
    auto resp = client.call("org.test.M", {{"n", 5}}, Client::CallMode::Basic)();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.MethodNotImplemented");
    EXPECT_EQ(resp["parameters"]["method"].get<string>(), "org.test.M");
}

TEST(UnixSocket, orgtestMoreThread) {
    using namespace std::chrono;
    using namespace std::chrono_literals;
    auto client = Client("test-integration.socket");
    auto client2 = Client("test-integration.socket");
    auto begin = steady_clock::now();
    auto more = client.call("org.test.M", {{"n", 1}, {"t", true}}, Client::CallMode::More);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 0);
    auto begin2 = steady_clock::now();
    auto resp = client2.call("org.test.P", {{"p", "test"}})();
    auto done2 = steady_clock::now();
    EXPECT_EQ(resp["parameters"]["q"].get<string>(), "test");
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 1);
    EXPECT_EQ(more(), nullptr);
    auto time_to_send = begin2 - begin;
    auto latency = done2 - begin2;
    EXPECT_LT(time_to_send, 1ms);
    EXPECT_LT(latency, 1ms);
}
TEST(UnixSocket, orgtestMoreAndQuit) {
    auto client = Client("test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, Client::CallMode::More);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 0);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 1);
    EXPECT_EQ(more()["parameters"]["m"].get<int>(), 2);
}

TEST(UnixSocket, orgtestOneway) {
    auto client = Client("test-integration.socket");
    auto null = client.call("org.test.P", {{"p", "test"}}, Client::CallMode::Oneway);
    EXPECT_EQ(null(), nullptr);
}

TEST(UnixSocket, InterfaceNotFound) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.notfound.NonExistent", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(resp["parameters"]["interface"].get<string>(), "org.notfound");
    resp = client.call("org.notfound", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(resp["parameters"]["interface"].get<string>(), "org");
    resp = client.call("org", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(resp["parameters"]["interface"].get<string>(), "org");
}

TEST(UnixSocket, MethodNotFound) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.test.NonExistent", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.MethodNotFound");
    EXPECT_EQ(resp["parameters"]["method"].get<string>(), "org.test.NonExistent");
}

TEST(UnixSocket, InvalidMessage) {
    auto client = JsonConnection<socket::UnixSocket>(socket::Mode::Connect, "test-integration.socket");
    client.send(R"({"notmethod":"org.test.NonExistent"})"_json);
    EXPECT_THROW((void)client.receive(), std::system_error);
}

TEST(UnixSocket, InvalidParameterType) {
    auto client = JsonConnection<socket::UnixSocket>(socket::Mode::Connect, "test-integration.socket");
    client.send(R"({"method":"org.test.P","parameters":["array"]})"_json);
    EXPECT_THROW((void)client.receive(), std::system_error);
}

TEST(UnixSocket, NotJsonMessage) {
    auto client = socket::UnixSocket(socket::Mode::Connect, "test-integration.socket");
    std::string data = "{totally_not\"a json-message]";
    client.write(data.begin(), data.end() + 1);
    EXPECT_THROW((void)client.read(data.begin(), data.end()), std::system_error);
}

TEST(UnixSocket, Nothing) {
    auto client1 = Client("test-integration.socket");
    auto client2 = Client("test-integration.socket");
    auto client3 = Client("test-integration.socket");
    auto client4 = Client("test-integration.socket");
}

TEST(UnixSocket, InternalException) {
    auto client = Client("test-integration.socket");
    auto resp = client.call("org.err.E", {})();
    EXPECT_EQ(resp["error"].get<string>(), "org.varlink.service.InternalError");
}

GTEST_API_ int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    printf("Running main() from %s\n", __FILE__);
    testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new Environment());
    return RUN_ALL_TESTS();
}