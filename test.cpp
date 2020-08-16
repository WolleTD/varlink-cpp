#include <iostream>
#include <string_view>
#include <varlink.hpp>
#include "org.example.more.varlink.cpp.inc"

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
