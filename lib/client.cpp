#include <varlink.hpp>

using namespace varlink;

Client::Client(const std::string &address) : conn(address) {}

std::function<nlohmann::json()> Client::call(const std::string& method, const nlohmann::json& parameters,
                                             CallMode mode) {
    nlohmann::json message { { "method", method } };
    if (!parameters.empty()) {
        message["parameters"] = parameters;
    }

    if (mode == CallMode::Oneway) {
        message["oneway"] = true;
    } else if (mode == CallMode::More) {
        message["more"] = true;
    } else if (mode == CallMode::Upgrade) {
        message["upgrade"] = true;
    }

    conn.send(message);

    return [this, mode, continues = true]() mutable -> nlohmann::json {
        if (mode != CallMode::Oneway && continues) {
            auto reply = conn.receive();
            if (mode == CallMode::More && reply.contains("continues")) {
                continues = reply["continues"];
            } else {
                continues = false;
            }
            return reply;
        } else {
            return {};
        }
    };
}
