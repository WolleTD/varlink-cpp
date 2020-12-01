#include <iostream>
#include <string>
#include <varlink/client.hpp>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: varlink socket path required\n";
        return 1;
    }
    varlink::net::io_context ctx{};
    auto client = varlink::varlink_client(ctx, argv[1]);

    try {
        auto resp = client.call("org.varlink.certification.Start", {})();
        std::cout << "Start: " << resp.dump() << std::endl;
        auto client_id = resp["client_id"].get<std::string>();

        std::vector<std::string> regular_tests = {"Test01", "Test02", "Test03", "Test04", "Test05",
                                                  "Test06", "Test07", "Test08", "Test09"};
        for (const auto& method : regular_tests) {
            resp = client.call("org.varlink.certification." + method, resp)();
            std::cout << method << ": " << resp.dump() << std::endl;
            resp["client_id"] = client_id;
        }

        auto more = client.call("org.varlink.certification.Test10", resp, varlink::callmode::more);
        varlink::json call11 = {{"client_id", client_id}};
        call11["last_more_replies"] = varlink::json::array();
        resp = more();
        while (!resp.is_null()) {
            std::cout << "Test10: " << resp.dump() << std::endl;
            call11["last_more_replies"].push_back(resp["string"].get<std::string>());
            resp = more();
        }

        resp = client.call("org.varlink.certification.Test11", call11, varlink::callmode::oneway)();
        std::cout << "Test11: " << resp.dump() << std::endl;

        resp = client.call("org.varlink.certification.End", varlink::json{{"client_id", client_id}})();
        std::cout << "End: " << resp.dump() << std::endl;
        return (resp["all_ok"].get<bool>()) ? 0 : 1;
    } catch (varlink::varlink_error& e) {
        std::cout << "Failed: " << e.what() << " parameters: " << e.args().dump() << std::endl;
        return 1;
    }
}