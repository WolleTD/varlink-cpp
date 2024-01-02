#include <future>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <varlink/async_client.hpp>

#include "fake_socket.hpp"

#ifdef LIBVARLINK_USE_BOOST
#include <boost/asio/thread_pool.hpp>
#else
#include <asio/thread_pool.hpp>
#endif

using namespace varlink;
using test_client = async_client<fake_proto>;

TEST_CASE("Client sync call processing")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    std::shared_ptr<test_client> client{};

    auto setup_test = [&](const auto& expected_call, const auto& response) {
        socket.setup_fake(response);
        socket.expect(expected_call);
        client = std::make_shared<test_client>(std::move(socket));
    };

    SECTION("Simple call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        auto resp = client->call("org.test.Test", {{"ping", "123"}});
        REQUIRE(resp["pong"].get<std::string>() == "123");
    }

    SECTION("Oneway call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","oneway":true,"parameters":{"ping":"123"}})",
            net::buffer(&client, 0));
        client->call_oneway("org.test.Test", {{"ping", "123"}});
    }

    SECTION("More call")
    {
        socket.validate = true;
        std::string reply = R"({"continues":true,"parameters":{"pong":"123"}})";
        reply += '\0';
        reply += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(R"({"method":"org.test.Test","more":true,"parameters":{"ping":"123"}})", reply);
        auto resp = client->call_more("org.test.Test", {{"ping", "123"}});
        REQUIRE(resp()["pong"].get<std::string>() == "123");
        REQUIRE(resp()["pong"].get<std::string>() == "123");
        REQUIRE(resp() == nullptr);
    }

    SECTION("Error returned")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"error":"org.test.Error","parameters":{"test":1337}})");
        try {
            auto resp = client->call("org.test.Test", {{"ping", "123"}});
            REQUIRE(false);
        }
        catch (varlink_error& e) {
            REQUIRE(e.type() == "org.test.Error");
            REQUIRE(e.params()["test"].get<int>() == 1337);
        }
    }

    SECTION("End of file")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            net::buffer(std::string_view(R"({"parameters":{"pong":)")));
        try {
            auto resp = client->call("org.test.Test", {{"ping", "123"}});
            REQUIRE(false);
        }
        catch (std::system_error& e) {
            REQUIRE(e.code() == net::error::eof);
        }
        catch (std::exception&) {
            // Unexpected exception
            REQUIRE(false);
        }
    }

    SECTION("Incomplete message")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})", R"({"parameters":{"pong":)");
        try {
            auto resp = client->call("org.test.Test", {{"ping", "123"}});
            REQUIRE(false);
        }
        catch (std::invalid_argument& e) {
            REQUIRE(std::string_view(e.what()).empty());
        }
    }

    SECTION("Broken pipe")
    {
        socket.error_on_write = true;
        setup_test("", "");
        REQUIRE_THROWS_AS(client->call("org.test.Test", {{"ping", "123"}}), std::system_error);
    }
}

TEST_CASE("Client async call processing")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    std::shared_ptr<test_client> client{};

    auto setup_test = [&](const auto& expected_call, const auto& response) {
        socket.setup_fake(response);
        socket.expect(expected_call);
        client = std::make_shared<test_client>(std::move(socket));
    };

    SECTION("Simple call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        bool flag{false};
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto eptr, const json& reply) {
            REQUIRE(not eptr);
            REQUIRE(reply["pong"].get<std::string>() == "123");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Oneway call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","oneway":true,"parameters":{"ping":"123"}})",
            net::buffer(&client, 0));
        bool flag{false};
        auto msg = varlink_message_oneway("org.test.Test", {{"ping", "123"}});
        client->async_call_oneway(msg, [&](auto eptr) {
            REQUIRE(not eptr);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("More call")
    {
        socket.validate = true;
        std::string reply = R"({"continues":true,"parameters":{"pong":"123"}})";
        reply += '\0';
        reply += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(R"({"method":"org.test.Test","more":true,"parameters":{"ping":"123"}})", reply);
        bool flag{false};
        bool was_more{false};
        auto msg = varlink_message_more("org.test.Test", {{"ping", "123"}});
        auto move_only_check = std::make_unique<int>();
        client->async_call_more(
            msg, [&, moc = std::move(move_only_check)](auto eptr, const json& r, bool more) {
                REQUIRE(moc);
                REQUIRE(not eptr);
                REQUIRE(was_more == not more);
                was_more = more;
                REQUIRE(r["pong"].get<std::string>() == "123");
                if (not more) flag = true;
            });
        REQUIRE(ctx.run() > 0);
        REQUIRE(not was_more);
        REQUIRE(flag);
    }

    SECTION("Error returned")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"error":"org.test.Error","parameters":{"test":1337}})");
        bool flag{false};
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](std::exception_ptr eptr, const json& r) {
            REQUIRE(eptr);
            try {
                std::rethrow_exception(eptr);
            }
            catch (varlink_error& e) {
                flag = true;
                REQUIRE(e.type() == "org.test.Error");
                REQUIRE(e.params()["test"].get<int>() == 1337);
            }
            REQUIRE(r["test"].get<int>() == 1337);
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("End of file")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            net::buffer(std::string_view(R"({"parameters":{"pong":)")));
        bool flag{false};
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto eptr, const json& r) {
            REQUIRE(eptr);
            try {
                std::rethrow_exception(eptr);
            }
            catch (std::system_error& e) {
                flag = true;
                REQUIRE(e.code() == net::error::eof);
            }
            REQUIRE(r == nullptr);
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Incomplete message")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})", R"({"parameters":{"pong":)");
        bool flag{false};
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto eptr, const json& r) {
            REQUIRE(eptr);
            std::error_code ec;
            try {
                std::rethrow_exception(eptr);
            }
            catch (std::system_error& e) {
                ec = e.code();
            }
            REQUIRE(ec == net::error::invalid_argument);
            REQUIRE(r == nullptr);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Broken pipe")
    {
        socket.error_on_write = true;
        setup_test("", "");
        bool flag{false};
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto eptr, const json& r) {
            REQUIRE(eptr);
            std::error_code ec;
            try {
                std::rethrow_exception(eptr);
            }
            catch (std::system_error& e) {
                ec = e.code();
            }
            REQUIRE(ec == net::error::broken_pipe);
            REQUIRE(r == nullptr);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }
}

TEST_CASE("client: Executor correctness")
{
    net::io_context ctx;
    net::thread_pool tp(1);

    std::promise<std::thread::id> p_tid;
    auto f_tid = p_tid.get_future();
    post(tp, [&p_tid]() { p_tid.set_value(std::this_thread::get_id()); });
    auto this_tid = std::this_thread::get_id();
    auto pool_tid = f_tid.get();

    auto sock = FakeSocket(ctx);

    auto setup_test = [&](const auto& expected_call, const auto& response) {
        sock.setup_fake(response);
        sock.expect(expected_call);
        return test_client(std::move(sock));
    };

    p_tid = {};
    f_tid = p_tid.get_future();

    SECTION("Call: no binding")
    {
        auto conn = setup_test(
            R"({"method":"org.test.Test","parameters":{}})", R"({"parameters":{}})");
        auto msg = varlink_message("org.test.Test", {});
        conn.async_call(msg, [&](auto eptr, const json&) {
            REQUIRE(not eptr);
            p_tid.set_value(std::this_thread::get_id());
        });
        ctx.run();
        REQUIRE(f_tid.get() == this_tid);
    }

    SECTION("Call: bind threadpool")
    {
        auto conn = setup_test(
            R"({"method":"org.test.Test","parameters":{}})", R"({"parameters":{}})");
        auto msg = varlink_message("org.test.Test", {});
        conn.async_call(msg, bind_executor(tp, [&](auto eptr, const json&) {
                            REQUIRE(not eptr);
                            p_tid.set_value(std::this_thread::get_id());
                        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }

    SECTION("Call: bind threadpool (local error)")
    {
        sock.error_on_write = true;
        auto conn = setup_test(
            R"({"method":"org.test.Test","parameters":{}})", R"({"parameters":{}})");
        auto msg = varlink_message("org.test.Test", {});
        conn.async_call(msg, bind_executor(tp, [&](auto eptr, const json&) {
                            REQUIRE(eptr);
                            std::error_code ec;
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch (std::system_error& e) {
                                ec = e.code();
                            }
                            REQUIRE(ec == net::error::broken_pipe);
                            p_tid.set_value(std::this_thread::get_id());
                        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }

    SECTION("Call: bind threadpool (varlink error)")
    {
        auto conn = setup_test(
            R"({"method":"org.test.Test","parameters":{}})",
            R"({"error":"org.test.Error","parameters":{}})");
        auto msg = varlink_message("org.test.Test", {});
        conn.async_call(msg, bind_executor(tp, [&](auto eptr, const json&) {
                            REQUIRE(eptr);
                            std::string err;
                            try {
                                std::rethrow_exception(eptr);
                            }
                            catch (varlink_error& e) {
                                err = e.type();
                            }
                            REQUIRE(err == "org.test.Error");
                            p_tid.set_value(std::this_thread::get_id());
                        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }
}
