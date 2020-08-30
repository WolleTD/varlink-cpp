#include <varlink.hpp>

using namespace varlink;

Client::Client(const std::string &address) : conn(address) {}

std::function<json()> Client::call(const std::string& method, const json& parameters,
                                             CallMode mode) {
    json message { { "method", method } };
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

    return [this, mode, continues = true]() mutable -> json {
        if (mode != CallMode::Oneway && continues) {
            auto reply = conn.receive();
            if (mode == CallMode::More && reply.contains("continues")) {
                continues = reply["continues"].get<bool>();
            } else {
                continues = false;
            }
            return reply;
        } else {
            return {};
        }
    };
}
