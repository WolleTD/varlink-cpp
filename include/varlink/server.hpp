/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <thread>
#include <utility>

#include "varlink/common.hpp"
#include "varlink/interface.hpp"
#include "varlink/message.hpp"
#include "varlink/service.hpp"

namespace varlink {

    class ListeningSocket {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        int listen_fd { -1 };
    public:
        explicit ListeningSocket(std::string address, const std::function<void()>& listener = nullptr);
        ListeningSocket(const ListeningSocket& src) = delete;
        ListeningSocket& operator=(const ListeningSocket&) = delete;
        ListeningSocket(ListeningSocket&& src) noexcept;
        ListeningSocket& operator=(ListeningSocket&& rhs) noexcept;
        ~ListeningSocket();

        [[nodiscard]] int nextClientFd();
        void listen(const std::function<void()>& listener);
    };


    template<typename ListenConnT, typename ClientConnT>
class BasicServer {
private:
    std::unique_ptr<ListenConnT> serviceConnection;
    Service service;

    void acceptLoop() {
        for(;;) {
            try {
                std::thread{[this](int fd) {
                    clientLoop(ClientConnT(fd));
                }, serviceConnection->nextClientFd()}.detach();
            } catch (std::system_error& e) {
                if (e.code() == std::errc::invalid_argument) {
                    // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                    break;
                } else {
                    std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
                }
            }
        }
    }

    // Template dependency: ClientConnection
    void clientLoop(ClientConnT conn) {
        const auto sendmore = [&conn](const json &msg) {
            assert(msg.is_object());
            conn.send({{"parameters", msg},
                       {"continues",  true}});
        };

        for (;;) {
            try {
                const Message message{conn.receive()};
                const auto reply = service.messageCall(message, sendmore);
                if (reply.is_object()) {
                    conn.send(reply);
                }
            } catch(std::invalid_argument& e) {
                std::cerr << "Invalid message: " << e.what() << std::endl;
                break;
            } catch(std::system_error& e) {
                if (e.code() != std::error_code(0, std::system_category())) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                }
                break;
            }
        }
    }

public:
    BasicServer(const std::string& address, const std::string& vendor, const std::string& product,
                const std::string& version, const std::string& url)
            : serviceConnection(std::make_unique<ListenConnT>(address, [this](){acceptLoop();})),
              service(vendor, product, version, url) {}
    explicit BasicServer(std::unique_ptr<ListenConnT> listenConn)
            : serviceConnection(std::move(listenConn)), service() {
        serviceConnection->listen([this]() { acceptLoop(); });
    }
    /*BasicServer(BasicServer &&src) noexcept :
            listeningThread(std::exchange(src.listeningThread, {})) {}

    BasicServer& operator=(BasicServer &&rhs) noexcept {
        BasicServer s(std::move(rhs));
        std::swap(listeningThread, s.listeningThread);
        return *this;
    }*/

    void addInterface(const Interface& interface) { service.addInterface(interface); }
    void addInterface(std::string_view interface, const CallbackMap& callbacks) {
        service.addInterface(Interface(interface, callbacks));
    }

};
}

#endif // LIBVARLINK_VARLINK_SERVER_HPP
