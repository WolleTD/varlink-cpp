#include <iostream>
#include <string>
#include <varlink/client.hpp>

using namespace varlink;
using std::string;

struct certification_client {
    certification_client(net::io_context& ctx, std::string_view uri)
        : client_(ctx, uri),
          test_methods(
              {"Start",
               "Test01",
               "Test02",
               "Test03",
               "Test04",
               "Test05",
               "Test06",
               "Test07",
               "Test08",
               "Test09",
               "Test10",
               "Test11",
               "End"})
    {
    }

    void start() { call_method(0, {}); }

    [[nodiscard]] int exit_code() const { return all_ok ? 0 : 1; }

  private:
    void call_method(size_t method_idx, const varlink::json& params)
    {
        const auto& method_name = test_methods.at(method_idx);
        client_.async_call(
            "org.varlink.certification." + method_name,
            params,
            [this, method_idx, &method_name](auto ec, varlink::json resp) {
                std::cout << method_name << ": " << resp << std::endl;
                if (ec) { throw varlink_error(resp["error"].get<string>(), resp["parameters"]); }
                if (method_idx == 0) { client_id = resp["client_id"].get<string>(); }
                else {
                    resp["client_id"] = client_id;
                }
                if (method_idx == 11) { resp = json{{"client_id", client_id}}; }
                if (method_idx == 9) { call_method_more(method_idx + 1, resp); }
                else if ((method_idx + 1) < test_methods.size()) {
                    call_method(method_idx + 1, resp);
                }
                else {
                    all_ok = resp["all_ok"].get<bool>();
                }
            });
    }

    void call_method_more(size_t method_idx, const varlink::json& params)
    {
        const auto& method_name = test_methods.at(method_idx);
        client_.async_call_more(
            "org.varlink.certification." + method_name,
            params,
            [this, &method_name](auto ec, varlink::json resp, bool continues) {
                std::cout << method_name << ": " << resp << std::endl;
                if (ec) { throw varlink_error(resp["error"].get<string>(), resp["parameters"]); }
                resp["client_id"] = client_id;
                more_replies.push_back(resp["string"].get<string>());
                if (not continues) {
                    resp["last_more_replies"] = more_replies;
                    call_method_oneway(11, resp);
                }
            });
    }

    void call_method_oneway(size_t method_idx, const varlink::json& params)
    {
        const auto& method_name = test_methods.at(method_idx);
        client_.async_call_oneway(
            "org.varlink.certification." + method_name, params, [this, &method_name](auto ec) {
                std::cout << method_name << std::endl;
                if (ec) { throw std::system_error(ec); }
                call_method(12, json{{"client_id", client_id}});
            });
    }

    varlink_client client_;
    string client_id;
    bool all_ok{false};
    std::vector<string> test_methods;
    std::vector<string> more_replies;
};

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Error: varlink socket path required\n";
        return 1;
    }
    net::io_context ctx{};
    auto client = certification_client(ctx, argv[1]);
    client.start();
    try {
        ctx.run();
        return client.exit_code();
    }
    catch (varlink_error& e) {
        std::cout << "Failed: " << e.what() << "\n";
    }
}
