#include <catch2/catch_test_macros.hpp>

#include <varlink/detail/message.hpp>

using namespace varlink;

TEST_CASE("Varlink message parse")
{
    SECTION("Test method and parameter type validation")
    {
        REQUIRE_NOTHROW(basic_varlink_message(R"({"method":""})"_json));
        REQUIRE_NOTHROW(basic_varlink_message(R"({"method":"test"})"_json));
        REQUIRE_NOTHROW(basic_varlink_message(R"({"method":"a.b.C"})"_json));
        REQUIRE_NOTHROW(basic_varlink_message(R"({"method":"a.b.C","parameters":{}})"_json));
        REQUIRE_NOTHROW(basic_varlink_message(R"({"method":"","parameters":{"a":1}})"_json));

        REQUIRE_THROWS_AS(basic_varlink_message(R"({"method":42})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"({"method":null})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"({"method":[]})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            basic_varlink_message(R"({"method":"","parameters":1})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            basic_varlink_message(R"({"method":"","parameters":null})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            basic_varlink_message(R"({"method":"","parameters":[]})"_json), std::invalid_argument);

        REQUIRE_THROWS_AS(basic_varlink_message(R"(null)"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"(42)"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"("string")"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"({})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(basic_varlink_message(R"({"parameters":{}})"_json), std::invalid_argument);
    }

    SECTION("Test parameters")
    {
        auto defaultParams = basic_varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.parameters() == json::object());
        auto withParams = basic_varlink_message(R"({"method":"","parameters":{"a":1}})"_json);
        REQUIRE(withParams.parameters() == R"({"a":1})"_json);
    }

    SECTION("Test more wire flag")
    {
        auto defaultParams = basic_varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.mode() != callmode::more);
        auto withMoreTrue = basic_varlink_message(R"({"method":"","more":true})"_json);
        REQUIRE(withMoreTrue.mode() == callmode::more);
        auto withMoreFalse = basic_varlink_message(R"({"method":"","more":false})"_json);
        REQUIRE(withMoreFalse.mode() != callmode::more);
    }

    SECTION("Test oneway wire flag")
    {
        auto defaultParams = basic_varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.mode() != callmode::oneway);
        auto withOnewayTrue = basic_varlink_message(R"({"method":"","oneway":true})"_json);
        REQUIRE(withOnewayTrue.mode() == callmode::oneway);
        auto withOnewayFalse = basic_varlink_message(R"({"method":"","oneway":false})"_json);
        REQUIRE(withOnewayFalse.mode() != callmode::oneway);
    }

    SECTION("Test upgrade wire flag")
    {
        auto defaultParams = basic_varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.mode() != callmode::upgrade);
        auto withOnewayTrue = basic_varlink_message(R"({"method":"","upgrade":true})"_json);
        REQUIRE(withOnewayTrue.mode() == callmode::upgrade);
        auto withOnewayFalse = basic_varlink_message(R"({"method":"","upgrade":false})"_json);
        REQUIRE(withOnewayFalse.mode() != callmode::upgrade);
    }

    SECTION("Message, InterfaceAndMethod")
    {
        struct TestData {
            std::string input;
            std::string interface;
            std::string method;
        };
        const std::vector<TestData> testdata{
            {"", "", ""},
            {"test", "test", "test"},
            {"a.b", "a", "b"},
            {"a.b.c.D", "a.b.c", "D"},
            {"org.varlink.service.GetInfo", "org.varlink.service", "GetInfo"},
            {"a.b.c.", "a.b.c", ""},
        };
        for (const auto& test : testdata) {
            auto msg = basic_varlink_message(json{{"method", test.input}});
            REQUIRE(msg.interface() == test.interface);
            REQUIRE(msg.method() == test.method);
        }
    }
}

TEST_CASE("Varlink message serialization")
{
    SECTION("Test parameters")
    {
        auto fromCallmode = varlink_message("org.test", {{"test", 1}});
        REQUIRE(fromCallmode.json_data() == R"({"method":"org.test","parameters":{"test":1}})"_json);
    }

    SECTION("Test upgrade wire flag")
    {
        auto fromCallmode = varlink_message_upgrade("org.test", {});
        REQUIRE(fromCallmode.json_data() == R"({"method":"org.test","upgrade":true})"_json);
    }

    SECTION("Test oneway wire flag")
    {
        auto fromCallmode = varlink_message_oneway("org.test", {});
        REQUIRE(fromCallmode.json_data() == R"({"method":"org.test","oneway":true})"_json);
    }

    SECTION("Test more wire flag")
    {
        auto fromCallmode = varlink_message_more("org.test", {});
        REQUIRE(fromCallmode.json_data() == R"({"method":"org.test","more":true})"_json);
    }
}
