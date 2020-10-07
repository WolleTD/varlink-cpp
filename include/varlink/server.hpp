/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <charconv>
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
class varlink_server {
   public:
    using ClientConnT = basic_json_connection<socket::type::unspec>;

   private:
    std::atomic_bool terminate{false};
    socket::variant listen_socket;
    varlink_service service;
    std::thread listen_thread;

    auto make_client_thread() {
        return [this](auto conn) {
            try {
                while (not terminate) {
                    const varlink_message message{conn.receive()};
                    const auto reply = service.message_call(message, [&](auto &&more) {
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
                if (e.code() != std::errc{}) {
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
            while (not terminate) {
                try {
                    auto client_task = std::packaged_task<void(ClientConnT)>{make_client_thread()};
                    clients.push_back(client_task.get_future());
                    std::thread{std::move(client_task),
                                ClientConnT(std::visit([](auto &&s) { return s.accept(nullptr); }, listen_socket))}
                        .detach();
                } catch (std::system_error &e) {
                    if (e.code() != std::errc::invalid_argument) {
                        std::cerr << e.what() << " (" << e.code() << ")\n";
                    }
                }
            }
            for (auto &&p : clients) p.wait();
        };
    }

   public:
    varlink_server(std::string_view uri, const varlink_service::description &description)
        : listen_socket(make_from_uri<socket::basic_socket>(varlink_uri(uri), socket::mode::listen)),
          service(description),
          listen_thread(make_listen_thread()) {}

    explicit varlink_server(socket::variant listenConn, const varlink_service::description &description)
        : listen_socket(std::move(listenConn)), service(description), listen_thread(make_listen_thread()) {}

    varlink_server(const varlink_server &src) = delete;
    varlink_server &operator=(const varlink_server &) = delete;
    varlink_server(varlink_server &&src) noexcept = delete;
    varlink_server &operator=(varlink_server &&src) noexcept = delete;

    ~varlink_server() {
        terminate = true;
        // Calling shutdown will release the thread from it's accept() call
        std::visit([&](auto &&sock) { sock.shutdown(SHUT_RDWR); }, listen_socket);
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
