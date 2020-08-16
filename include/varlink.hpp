/* Varlink C++ implementation using nlohmann/json as data format */

#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

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

        void send(const nlohmann::json& message);

        nlohmann::json receive();
    };

    class Service {
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
