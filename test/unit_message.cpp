#include <catch2/catch.hpp>

#include <varlink/varlink.hpp>

using namespace varlink;

TEST_CASE("Varlink message parse")
{
    SECTION("Test method and parameter type validation")
    {
        REQUIRE_NOTHROW(varlink_message(R"({"method":""})"_json));
        REQUIRE_NOTHROW(varlink_message(R"({"method":"test"})"_json));
        REQUIRE_NOTHROW(varlink_message(R"({"method":"a.b.C"})"_json));
        REQUIRE_NOTHROW(
            varlink_message(R"({"method":"a.b.C","parameters":{}})"_json));
        REQUIRE_NOTHROW(
            varlink_message(R"({"method":"","parameters":{"a":1}})"_json));

        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":42})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":null})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":[]})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":"","parameters":1})"_json),
            std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":"","parameters":null})"_json),
            std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"method":"","parameters":[]})"_json),
            std::invalid_argument);

        REQUIRE_THROWS_AS(varlink_message(R"(null)"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(varlink_message(R"(42)"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"("string")"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(varlink_message(R"({})"_json), std::invalid_argument);
        REQUIRE_THROWS_AS(
            varlink_message(R"({"parameters":{}})"_json), std::invalid_argument);
    }

    SECTION("Test parameters")
    {
        auto defaultParams = varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.parameters() == json::object());
        auto withParams = varlink_message(
            R"({"method":"","parameters":{"a":1}})"_json);
        REQUIRE(withParams.parameters() == R"({"a":1})"_json);
    }

    SECTION("Test more wire flag")
    {
        auto defaultParams = varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.more() == false);
        auto withMoreTrue = varlink_message(R"({"method":"","more":true})"_json);
        REQUIRE(withMoreTrue.more() == true);
        auto withMoreFalse = varlink_message(R"({"method":"","more":false})"_json);
        REQUIRE(withMoreFalse.more() == false);
    }

    SECTION("Test oneway wire flag")
    {
        auto defaultParams = varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.oneway() == false);
        auto withOnewayTrue = varlink_message(
            R"({"method":"","oneway":true})"_json);
        REQUIRE(withOnewayTrue.oneway() == true);
        auto withOnewayFalse = varlink_message(
            R"({"method":"","oneway":false})"_json);
        REQUIRE(withOnewayFalse.oneway() == false);
    }

    SECTION("Test upgrade wire flag")
    {
        auto defaultParams = varlink_message(R"({"method":""})"_json);
        REQUIRE(defaultParams.upgrade() == false);
        auto withOnewayTrue = varlink_message(
            R"({"method":"","upgrade":true})"_json);
        REQUIRE(withOnewayTrue.upgrade() == true);
        auto withOnewayFalse = varlink_message(
            R"({"method":"","upgrade":false})"_json);
        REQUIRE(withOnewayFalse.upgrade() == false);
    }

    SECTION("Message, InterfaceAndMethod")
    {
        struct TestData {
            std::string input;
            std::pair<std::string, std::string> output;
        };
        const std::vector<TestData> testdata{
            {"", {"", ""}},
            {"test", {"test", "test"}},
            {"a.b", {"a", "b"}},
            {"a.b.c.D", {"a.b.c", "D"}},
            {"org.varlink.service.GetInfo", {"org.varlink.service", "GetInfo"}},
            {"a.b.c.", {"a.b.c", ""}},
        };
        for (const auto& test : testdata) {
            auto msg = varlink_message(json{{"method", test.input}});
            REQUIRE(msg.interface_and_method() == test.output);
        }
    }
}

TEST_CASE("Varlink message serialization")
{
    SECTION("Test parameters")
    {
        auto fromCallmode = varlink_message("org.test", {{"test", 1}});
        REQUIRE(
            fromCallmode.json_data()
            == R"({"method":"org.test","parameters":{"test":1}})"_json);
    }

    SECTION("Test upgrade wire flag")
    {
        auto fromCallmode = varlink_message(
            "org.test", {}, varlink_message::callmode::upgrade);
        REQUIRE(
            fromCallmode.json_data()
            == R"({"method":"org.test","upgrade":true})"_json);
    }

    SECTION("Test oneway wire flag")
    {
        auto fromCallmode = varlink_message(
            "org.test", {}, varlink_message::callmode::oneway);
        REQUIRE(
            fromCallmode.json_data()
            == R"({"method":"org.test","oneway":true})"_json);
    }

    SECTION("Test more wire flag")
    {
        auto fromCallmode = varlink_message(
            "org.test", {}, varlink_message::callmode::more);
        REQUIRE(
            fromCallmode.json_data()
            == R"({"method":"org.test","more":true})"_json);
    }
}