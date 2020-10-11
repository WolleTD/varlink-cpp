#include <csignal>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <varlink/server.hpp>

#include "org.varlink.certification.varlink.hpp"

class varlink_certification {
   private:
    std::map<std::string, std::string> status;
    size_t clients{0};
    std::mutex start_mut;

    const varlink::json my_object = R"json({
                "object": {"method": "org.varlink.certification.Test09",
                        "parameters": {"map": {"foo": "Foo", "bar": "Bar"}}},
                "enum": "two",
                    "struct": {"first": 1, "second": "2"},
                "array": ["one", "two", "three"],
                "dictionary": {"foo": "Foo", "bar": "Bar"},
                "stringset": {"one":{}, "two":{}, "three":{}},
                    "interface": {
                    "foo": [
                        {},
                            {"foo": "foo", "bar": "bar"},
                        {},
                            {"one": "foo", "two": "bar"}
                    ],
                        "anon": {"foo": true, "bar": false}
                }
            })json"_json;

    std::string generate_client_id() { return "deadbeef" + std::to_string(clients++); }

    static varlink::varlink_error certification_error(const std::string& want, const std::string& got) {
        return {"org.varlink.certification.CertificationError", {{"want", want}, {"got", got}}};
    }

    std::string check_client_id(const varlink::json& parameters) {
        auto client_id = parameters["client_id"].get<std::string>();
        auto lk = std::lock_guard(start_mut);
        if (status.find(client_id) == status.cend()) {
            throw varlink::varlink_error("org.varlink.certification.ClientIdError", {});
        } else {
            return client_id;
        }
    }

    void assert_method(const std::string& client_id, const std::string& called_method, const std::string& next_method) {
        auto lk = std::lock_guard(start_mut);
        if (status[client_id] == called_method) {
            status[client_id] = next_method;
        } else {
            throw certification_error(status[client_id], called_method);
        }
    }

    static void assert_parameter(const varlink::json& parameters, const std::string& param,
                                 const varlink::json& expected) {
        auto value = parameters[param];
        if (value != expected) {
            throw certification_error(expected.dump(), value.dump());
        }
    }

   public:
    auto Start varlink_callback {
        auto client_id = generate_client_id();
        auto lk = std::lock_guard(start_mut);
        status[client_id] = "Test01";
        return {{"client_id", client_id}};
    };

    auto Test01 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test01", "Test02");
        return {{"bool", true}};
    };

    auto Test02 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test02", "Test03");
        assert_parameter(parameters, "bool", true);
        return {{"int", 1}};
    };

    auto Test03 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test03", "Test04");
        assert_parameter(parameters, "int", 1);
        return {{"float", 1.0}};
    };

    auto Test04 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test04", "Test05");
        assert_parameter(parameters, "float", 1.0);
        return {{"string", "ping"}};
    };

    auto Test05 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test05", "Test06");
        assert_parameter(parameters, "string", "ping");
        return {{"bool", false}, {"int", 2}, {"float", 3.14}, {"string", "a lot of string"}};
    };

    auto Test06 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test06", "Test07");
        assert_parameter(parameters, "bool", false);
        assert_parameter(parameters, "int", 2);
        assert_parameter(parameters, "float", 3.14);
        assert_parameter(parameters, "string", "a lot of string");
        return {{"struct", {{"bool", false}, {"int", 2}, {"float", 3.14}, {"string", "a lot of string"}}}};
    };

    auto Test07 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test07", "Test08");
        auto my_struct = parameters["struct"];
        assert_parameter(my_struct, "bool", false);
        assert_parameter(my_struct, "int", 2);
        assert_parameter(my_struct, "float", 3.14);
        assert_parameter(my_struct, "string", "a lot of string");
        return {{"map", {{"foo", "Foo"}, {"bar", "Bar"}}}};
    };

    auto Test08 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test08", "Test09");
        auto my_map = parameters["map"];
        assert_parameter(my_map, "foo", "Foo");
        assert_parameter(my_map, "bar", "Bar");
        return {
            {"set",
             {{"one", varlink::json::object()}, {"two", varlink::json::object()}, {"three", varlink::json::object()}}}};
    };

    auto Test09 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test09", "Test10");
        assert_parameter(
            parameters, "set",
            {{"one", varlink::json::object()}, {"two", varlink::json::object()}, {"three", varlink::json::object()}});
        return {{"mytype", my_object}};
    };

    auto Test10 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test10", "Test11");
        if (parameters["mytype"] != my_object) {
            throw certification_error(my_object.dump(), parameters["mytype"].dump());
        }
        for (int i = 1; i < 10; i++) {
            sendmore({{"string", "Reply number " + std::to_string(i)}});
        }
        return {{"string", "Reply number 10"}};
    };

    auto Test11 varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "Test11", "End");
        auto expected =
            varlink::json{"Reply number 1", "Reply number 2", "Reply number 3", "Reply number 4", "Reply number 5",
                          "Reply number 6", "Reply number 7", "Reply number 8", "Reply number 9", "Reply number 10"};
        if (parameters["last_more_replies"] != expected) {
            throw certification_error(expected.dump(), parameters.dump());
        }
        return {};
    };

    auto End varlink_callback {
        auto client_id = check_client_id(parameters);
        assert_method(client_id, "End", "Start");
        auto lk = std::lock_guard(start_mut);
        status.erase(client_id);
        return {{"all_ok", true}};
    };
};

#define varlink_callback_forward(callback) [&] varlink_callback { return callback(parameters, sendmore); }

std::unique_ptr<varlink::threaded_server> server;

void stop_server(int) {
    server.reset(nullptr);
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    if (argc < 2) {
        std::cerr << "Error: varlink socket path required\n";
        return 1;
    }
    server = std::make_unique<varlink::threaded_server>(argv[1], varlink::varlink_service::description{});
    auto cert = varlink_certification{};
    server->add_interface(varlink::org_varlink_certification_varlink,
                          varlink::callback_map{
                              {"Start", varlink_callback_forward(cert.Start)},
                              {"Test01", varlink_callback_forward(cert.Test01)},
                              {"Test02", varlink_callback_forward(cert.Test02)},
                              {"Test03", varlink_callback_forward(cert.Test03)},
                              {"Test04", varlink_callback_forward(cert.Test04)},
                              {"Test05", varlink_callback_forward(cert.Test05)},
                              {"Test06", varlink_callback_forward(cert.Test06)},
                              {"Test07", varlink_callback_forward(cert.Test07)},
                              {"Test08", varlink_callback_forward(cert.Test08)},
                              {"Test09", varlink_callback_forward(cert.Test09)},
                              {"Test10", varlink_callback_forward(cert.Test10)},
                              {"Test11", varlink_callback_forward(cert.Test11)},
                              {"End", varlink_callback_forward(cert.End)},
                          });
    server->join();
}