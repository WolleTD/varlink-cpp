/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <charconv>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename SocketT, typename ServiceT = Service>
class ThreadedServer {
   public:
    using ClientConnT = JsonConnection<SocketT>;

   private:
    std::unique_ptr<SocketT> listenSocket;
    std::unique_ptr<ServiceT> service;
    std::thread listenThread;

    auto messageProcessor() {
        return [&service = *service](ClientConnT &conn) {
          const auto sendmore = [&conn](const json &msg) {
            assert(msg.is_object());
            conn.send({{"parameters", msg}, {"continues", true}});
          };

          const Message message{conn.receive()};
          const auto reply = service.messageCall(message, sendmore);
          if (reply.is_object()) {
              conn.send(reply);
          }
        };
    }

    auto makeListenThread() {
        auto clientThread = [processConnection = messageProcessor()](auto conn) {
            try {
                while (true) processConnection(conn);
            } catch (std::system_error &e) {
                if (e.code() != std::error_code(0, std::system_category())) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                }
            } catch (std::invalid_argument &e) {
                std::cerr << "Couldn't read message: " << e.what() << std::endl;
            }
        };

        return [&sock = *listenSocket, clientThread]() {
            while (true) {
                try {
                    // TODO: don't detach, thread pool
                    std::thread{clientThread, ClientConnT(sock.accept(nullptr))}.detach();
                } catch (std::system_error &e) {
                    if (e.code() == std::errc::invalid_argument) {
                        // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                        break;
                    } else {
                        std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
                    }
                }
            }
        };
    }

   public:
    template <typename... Args>
    explicit ThreadedServer(const Service::Description &description, Args &&...args)
        : listenSocket(std::make_unique<SocketT>(socket::Mode::Listen, std::forward<Args>(args)...)),
          service(std::make_unique<ServiceT>(description)), listenThread(makeListenThread()) {}

    explicit ThreadedServer(std::unique_ptr<SocketT> listenConn, std::unique_ptr<ServiceT> existingService)
        : listenSocket(std::move(listenConn)), service(std::move(existingService)) {}

    ThreadedServer(const ThreadedServer &src) = delete;
    ThreadedServer &operator=(const ThreadedServer &) = delete;
    ThreadedServer(ThreadedServer &&src) noexcept = default;
    ThreadedServer &operator=(ThreadedServer &&src) noexcept = default;

    ~ThreadedServer() {
        // Calling shutdown will release the thread from it's accept() call
        if (listenSocket) listenSocket->shutdown(SHUT_RDWR);
        if (listenThread.joinable()) listenThread.join();
    }

    void join() { listenThread.join(); }

    template <typename... Args>
    void setInterface(Args &&...args) {
        service->setInterface(std::forward<Args>(args)...);
    }
};

using ThreadedUnixServer = ThreadedServer<socket::UnixSocket>;
using ThreadedTCPServer = ThreadedServer<socket::TCPSocket>;

class VarlinkServer {
   public:
    using ServerT = std::variant<ThreadedTCPServer, ThreadedUnixServer>;

   private:
    ServerT threadedServer;

    static ServerT makeServer(const VarlinkURI &uri, const Service::Description &description) {
        if (uri.type == VarlinkURI::Type::Unix) {
            return ThreadedUnixServer(description, uri.path);
        } else if (uri.type == VarlinkURI::Type::TCP) {
            uint16_t port{0};
            if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port); r.ptr != uri.port.end()) {
                throw std::invalid_argument("Invalid port");
            }
            return ThreadedTCPServer(description, uri.host, port);
        } else {
            throw std::invalid_argument("Unsupported protocol");
        }
    }

   public:
    VarlinkServer(std::string_view uri, const Service::Description &description)
        : threadedServer(makeServer(VarlinkURI(uri), description)) {}

    template <typename... Args>
    void setInterface(Args &&...args) {
        std::visit([&](auto &&srv) { srv.setInterface(std::forward<Args>(args)...); }, threadedServer);
    }

    void join() {
        std::visit([](auto &&srv) { srv.join(); }, threadedServer);
    }
};
}  // namespace varlink

#endif
