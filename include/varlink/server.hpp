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
class VarlinkServer {
   public:
    using SocketT = std::variant<socket::UnixSocket, socket::TCPSocket>;
    using ClientConnT = JsonConnection<socket::PosixSocket<socket::type::Unspecified> >;

   private:
    SocketT listenSocket;
    std::unique_ptr<Service> service;
    std::thread listenThread;

    auto makeClientThread() {
        return [&service = *service](auto conn) {
            try {
                while (true) {
                    const Message message{conn.receive()};
                    const auto reply = service.messageCall(message, [&conn](auto &&more) { conn.send(more); });
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

    auto makeListenThread() {
        return [&sock = listenSocket, clientThread = makeClientThread()]() {
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
    VarlinkServer(std::string_view uri, const Service::Description &description)
        : listenSocket(make_socket(socket::Mode::Listen, VarlinkURI(uri))),
          service(std::make_unique<Service>(description)),
          listenThread(makeListenThread()) {}

    explicit VarlinkServer(SocketT &&listenConn, std::unique_ptr<Service> existingService)
        : listenSocket(std::move(listenConn)), service(std::move(existingService)) {}

    VarlinkServer(const VarlinkServer &src) = delete;
    VarlinkServer &operator=(const VarlinkServer &) = delete;
    VarlinkServer(VarlinkServer &&src) noexcept = default;
    VarlinkServer &operator=(VarlinkServer &&src) noexcept = default;

    ~VarlinkServer() {
        // Calling shutdown will release the thread from it's accept() call
        std::visit([&](auto &&sock) { sock.shutdown(SHUT_RDWR); }, listenSocket);
        if (listenThread.joinable()) listenThread.join();
    }

    template <typename... Args>
    void setInterface(Args &&...args) {
        service->setInterface(std::forward<Args>(args)...);
    }

    void join() { listenThread.join(); }
};
}  // namespace varlink

#endif
