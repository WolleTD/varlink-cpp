/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>

namespace varlink {

    class Connection {
    private:
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;

        std::istream rstream { &filebuf_in };
        std::ostream wstream { &filebuf_out };

        int socket_fd { -1 };
    public:
        // Connect to a service via address
        explicit Connection(const std::string& address);

        // Setup message stream on existing connection
        explicit Connection(int posix_fd);

        Connection(const Connection& src) = delete;
        Connection(Connection&& src) noexcept;

        void send(const nlohmann::json& message);

        [[nodiscard]] nlohmann::json receive();
    };

    class Interface;

    using MethodCallback = std::function<nlohmann::json(nlohmann::json)>;

    class Service {
    public:
        struct Description {
            std::string vendor;
            std::string product;
            std::string version;
            std::string url;
        };
    private:
        std::string socketAddress;
        Description description;
        //std::vector<Interface> interfaces;
        std::thread listeningThread;
        int listen_fd { -1 };

        void dispatchConnections();
    public:
        Service(std::string address, Description desc);
        Service(const Service& src) = delete;
        Service(Service&& src) noexcept;
        ~Service();

        [[nodiscard]] Connection nextClientConnection() const;
    };

    class Client {
    private:
        Connection conn;
    public:
        enum class CallMode {
            Basic,
            Oneway,
            More,
            Upgrade,
        };
        explicit Client(const std::string& address);
        std::function<nlohmann::json()> call(const std::string& method,
                                             const nlohmann::json& parameters,
                                             CallMode mode = CallMode::Basic);
    };
}

#endif // LIBVARLINK_VARLINK_HPP
