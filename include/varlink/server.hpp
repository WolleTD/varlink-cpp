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
class BasicServer {
   public:
    using ClientConnT = JsonConnection<SocketT>;

   protected:
    std::unique_ptr<SocketT> listenSocket;
    std::unique_ptr<ServiceT> service;

    auto connectionAcceptor() {
        return [&sock = *listenSocket]() { return ClientConnT(sock.accept(nullptr)); };
    }

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

   public:
    template <typename... Args>
    explicit BasicServer(const typename ServiceT::Description &description, Args &&...args)
        : listenSocket(std::make_unique<SocketT>(socket::Mode::Listen, std::forward<Args>(args)...)),
          service(std::make_unique<ServiceT>(description)) {}

    explicit BasicServer(std::unique_ptr<SocketT> listenConn, std::unique_ptr<ServiceT> existingService)
        : listenSocket(std::move(listenConn)), service(std::move(existingService)) {}

    BasicServer(const BasicServer &src) = delete;
    BasicServer &operator=(const BasicServer &) = delete;
    BasicServer(BasicServer &&src) noexcept = default;
    BasicServer &operator=(BasicServer &&) noexcept = default;

    ~BasicServer() {
        if (listenSocket) listenSocket->shutdown(SHUT_RDWR);
    }

    ClientConnT accept() { return connectionAcceptor()(); }

    void processNextMessage(ClientConnT &conn) { messageProcessor()(conn); }

    template <typename... Args>
    void setInterface(Args &&...args) {
        service->setInterface(std::forward<Args>(args)...);
    }
};

template <typename SocketT, typename ServiceT = Service>
class ThreadedServer : BasicServer<SocketT, ServiceT> {
   public:
    using Base = BasicServer<SocketT>;
    using typename Base::ClientConnT;

   private:
    std::thread listenThread;

    auto makeListenThread() {
        auto clientThread = [processConnection = Base::messageProcessor()](auto conn) {
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

        return [accept = Base::connectionAcceptor(), clientThread]() {
            while (true) {
                try {
                    // TODO: don't detach, thread pool
                    std::thread{clientThread, accept()}.detach();
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
        : BasicServer<SocketT>(description, std::forward<Args>(args)...), listenThread(makeListenThread()) {}

    explicit ThreadedServer(std::unique_ptr<SocketT> listenConn, std::unique_ptr<ServiceT> service)
        : BasicServer<SocketT>(std::move(listenConn), std::move(service)) {}

    ThreadedServer(const ThreadedServer &src) = delete;
    ThreadedServer &operator=(const ThreadedServer &) = delete;
    ThreadedServer(ThreadedServer &&src) noexcept = default;
    ThreadedServer &operator=(ThreadedServer &&src) noexcept = default;

    ~ThreadedServer() {
        // There is no dedicated thread communication yet, so we use the socket to terminate
        // the listening thread TODO: thread pool will fix this
        if (Base::listenSocket) Base::listenSocket->shutdown(SHUT_RDWR);
        if (listenThread.joinable()) listenThread.join();
    }

    void join() { listenThread.join(); }

    using Base::setInterface;
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
            uint16_t port;
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
