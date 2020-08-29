#include <iostream>
#include <string_view>
#include <thread>
#include <sstream>
#include <chrono>
#include <memory>
#include <signal.h>
#include <varlink.hpp>
#include "org.example.more.varlink.cpp.inc"

#define TEST_SERVICE
//#define TEST_SCANNER
struct TestData {
    std::string method;
    nlohmann::json parameters;
    varlink::Client::CallMode mode {varlink::Client::CallMode::Basic };
};

constexpr std::string_view getInterfaceName(const std::string_view description) {
    constexpr std::string_view searchKey {"interface "};
    const size_t posInterface { description.find(searchKey) + searchKey.length() };
    const size_t lenInterface { description.find("\n", posInterface) - posInterface };
    return description.substr(posInterface, lenInterface);
}

std::unique_ptr<varlink::Service> service;

void signalHandler(int32_t signal) {
    service.reset(nullptr);
    exit(128 + signal);
}

#ifdef TEST_SCANNER
int main() {
    const auto start = std::chrono::system_clock::now();
    varlink::Interface interface(std::string{org_example_more_varlink});
    const auto duration = std::chrono::system_clock::now() - start;
    std::cout << "\n\n===========\n\n" << interface
        << "\nParsing took " << duration.count() << "ns\n";
    return 0;
}
#elif defined(TEST_SERVICE)
int main() {
    using namespace std::chrono_literals;
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    service = std::make_unique<varlink::Service>("/tmp/test.socket",
                                                 varlink::Service::Description{"a", "b", "c", "d" });
    service->addInterface(varlink::Interface{
        std::string(org_example_more_varlink),
        {
            {"Ping", VarlinkCallback {
                return varlink::reply({{"pong", message["parameters"]["ping"]}});
            }},
            {"TestMore", VarlinkCallback {
                if (message["parameters"].contains("n") && message["more"]) {
                    nlohmann::json state = {{"start", true}};
                    connection.send(varlink::reply_continues({{"state", state}}));
                    state.erase("start");
                    auto n = message["parameters"]["n"].get<size_t>();
                    for(size_t i = 0; i < n; i++) {
                        state["progress"] = (100 / n) * i;
                        connection.send(varlink::reply_continues({{"state", state}}));
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    state["progress"] = 100;
                    connection.send(varlink::reply_continues({{"state", state}}));
                    state.erase("progress");
                    state["end"] = true;
                    return varlink::reply_continues({{"state", state}}, false);
                } else {
                    return varlink::error("org.varlink.service.InvalidParameter",
                                          {{"parameter", "n"}});
                }
            }}
        } });
    std::cout << "waiting...\n";
    std::this_thread::sleep_for(200s);
    std::cout << "done\n";
    return 0;
}
#else
int main() {
    varlink::Client client("/tmp/test.sock1");

    for(const auto& test : std::vector<TestData>{
            {"org.varlink.service.GetInfo", {}},
            {"org.example.more.Ping", R"({"ping":"Test"})"_json},
            {"org.example.more.TestMore", R"({"n":10})"_json, varlink::Client::CallMode::More},
            {"org.example.more.Ping", R"({"ping":"Toast"})"_json},
            {"org.example.more.TestMore", R"({"n":4})"_json, varlink::Client::CallMode::More},
    }) {
        const auto receive = client.call(test.method, test.parameters, test.mode);
        std::cout << "Call: " << test.method << "\nParameters: " << test.parameters.dump(2) << "\n";
        auto message = receive();
        while (!message.empty()) {
            std::cout << "Reply: " << message.dump(2) << std::endl;
            message = receive();
        }
    }

    return 0;
}
#endif