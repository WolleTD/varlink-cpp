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
    std::unique_ptr<socket::variant> listen_socket;
    std::unique_ptr<varlink_service> service;
    std::thread listen_thread;

    auto make_client_thread() {
        return [&service = *service](auto conn) {
            try {
                while (true) {
                    const varlink_message message{conn.receive()};
                    const auto reply = service.message_call(message, [&conn](auto &&more) { conn.send(more); });
                    if (reply.is_object()) {
                        conn.send(reply);
                    }
                }
            } catch (std::system_error &e) {
                if (e.code() != std::error_code(0, std::system_category())) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                }
            } catch (std::invalid_argument &e) {
                std::cerr << "Couldn't read message: " << e.what() << std::endl;
            }
        };
    }

    auto make_listen_thread() {
        return [&sock = *listen_socket, clientThread = make_client_thread()]() {
            std::vector<std::future<void> > clients{};
            while (true) {
                try {
                    auto client_task = std::packaged_task<void(ClientConnT)>{clientThread};
                    clients.push_back(client_task.get_future());
                    std::thread{std::move(client_task),
                                ClientConnT(std::visit([](auto &&s) { return s.accept(nullptr); }, sock))}
                        .detach();
                } catch (std::system_error &e) {
                    if (e.code() == std::errc::invalid_argument) {
                        // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                        break;
                    } else {
                        std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
                    }
                }
            }
            for (auto &&p : clients) p.wait();
        };
    }

   public:
    varlink_server(std::string_view uri, const varlink_service::description &description)
        : listen_socket(std::make_unique<socket::variant>(
              make_from_uri<socket::basic_socket>(varlink_uri(uri), socket::mode::listen))),
          service(std::make_unique<varlink_service>(description)),
          listen_thread(make_listen_thread()) {}

    explicit varlink_server(std::unique_ptr<socket::variant> listenConn, std::unique_ptr<varlink_service> existingService)
        : listen_socket(std::move(listenConn)), service(std::move(existingService)) {}

    varlink_server(const varlink_server &src) = delete;
    varlink_server &operator=(const varlink_server &) = delete;
    varlink_server(varlink_server &&src) noexcept = default;
    varlink_server &operator=(varlink_server &&src) noexcept = default;

    ~varlink_server() {
        // Calling shutdown will release the thread from it's accept() call
        if (listen_socket) std::visit([&](auto &&sock) { sock.shutdown(SHUT_RDWR); }, *listen_socket);
        if (listen_thread.joinable()) listen_thread.join();
    }

    template <typename... Args>
    void add_interface(Args &&...args) {
        service->add_interface(std::forward<Args>(args)...);
    }

    void join() { listen_thread.join(); }
};
}  // namespace varlink

#endif
