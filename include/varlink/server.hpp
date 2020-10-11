/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <charconv>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename Acceptor, typename = std::enable_if_t<std::is_base_of_v<asio::socket_base, Acceptor> > >
class basic_server {
   public:
    using acceptor_type = Acceptor;
    using protocol_type = typename Acceptor::protocol_type;
    using socket_type = typename protocol_type::socket;
    using Connection = json_connection<socket_type>;

   private:
    Acceptor listen_socket;

   public:
    explicit basic_server(Acceptor acceptor) : listen_socket(std::move(acceptor)) {}

    basic_server(const basic_server &src) = delete;
    basic_server &operator=(const basic_server &) = delete;
    basic_server(basic_server &&src) noexcept = default;
    basic_server &operator=(basic_server &&src) noexcept = default;

    Connection accept() { return Connection(listen_socket.accept()); }

    void shutdown() { ::shutdown(listen_socket.native_handle(), SHUT_RDWR); }

    ~basic_server() {
        shutdown();
        if constexpr (std::is_same_v<protocol_type, asio::local::stream_protocol>) {
            if (listen_socket.is_open()) {
                std::filesystem::remove(listen_socket.local_endpoint().path());
            }
        }
    }
};

using json_acceptor_unix = basic_server<asio::local::stream_protocol::acceptor>;
using json_acceptor_tcp = basic_server<asio::ip::tcp::acceptor>;
using json_acceptor_variant = std::variant<json_acceptor_unix, json_acceptor_tcp>;

class threaded_server {
   private:
    std::atomic_bool terminate{false};
    asio::io_context ctx;
    json_acceptor_variant server;
    varlink_service service;
    std::thread listen_thread;

    auto make_client_thread() {
        return [this](auto conn) {
            try {
                while (not terminate) {
                    const varlink_message message{conn.receive()};
                    const auto reply = service.message_call(message, [&conn, this](auto &&more) {
                        if (terminate) {
                            throw std::system_error(std::make_error_code(std::errc::connection_aborted));
                        } else {
                            conn.send(more);
                        }
                    });
                    if (reply.is_object()) {
                        conn.send(reply);
                    }
                }
            } catch (std::system_error &e) {
                if ((e.code() != asio::error::eof) and (e.code() != asio::error::broken_pipe)) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                }
            } catch (std::invalid_argument &e) {
                std::cerr << "Couldn't read message: " << e.what() << std::endl;
            }
        };
    }

    auto make_listen_thread() {
        return [this]() {
            std::vector<std::future<void> > clients{};
            const auto accept_and_detach = [&](auto &&s) {
                using Connection = typename std::decay_t<decltype(s)>::Connection;
                auto client_task = std::packaged_task<void(Connection)>{make_client_thread()};
                clients.push_back(client_task.get_future());
                std::thread{std::move(client_task), Connection(s.accept())}.detach();
            };
            while (not terminate) {
                try {
                    std::visit(accept_and_detach, server);
                } catch (std::system_error &e) {
                    if (e.code() != asio::error::invalid_argument) {
                        std::cerr << e.what() << " (" << e.code() << ")\n";
                    }
                }
            }
            for (auto &&p : clients) p.wait();
        };
    }

    auto make_acceptor(const varlink_uri &uri) {
        return std::visit(
            [&](auto &&sockaddr) -> json_acceptor_variant {
                using Acceptor = typename std::decay_t<decltype(sockaddr)>::protocol_type::acceptor;
                return basic_server(Acceptor{ctx, sockaddr});
            },
            endpoint_from_uri(uri));
    }

   public:
    threaded_server(const varlink_uri &uri, const varlink_service::description &description)
        : ctx(1), server(make_acceptor(uri)), service(description), listen_thread(make_listen_thread()) {}

    threaded_server(std::string_view uri, const varlink_service::description &description)
        : threaded_server(varlink_uri(uri), description) {}

    threaded_server(const threaded_server &src) = delete;
    threaded_server &operator=(const threaded_server &) = delete;
    threaded_server(threaded_server &&src) noexcept = delete;
    threaded_server &operator=(threaded_server &&src) noexcept = delete;

    void stop_serving() {
        terminate = true;
        // Calling shutdown will release the thread from it's accept() call
        std::visit([](auto &&s) { s.shutdown(); }, server);
    }

    ~threaded_server() {
        if (not terminate) stop_serving();
        if (listen_thread.joinable()) listen_thread.join();
    }

    template <typename... Args>
    void add_interface(Args &&...args) {
        service.add_interface(std::forward<Args>(args)...);
    }

    void join() { listen_thread.join(); }
};
}  // namespace varlink

#endif
