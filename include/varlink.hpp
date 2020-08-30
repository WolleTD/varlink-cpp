/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>

#define VarlinkCallback \
    ([[maybe_unused]] const nlohmann::json& message, [[maybe_unused]] varlink::Connection& connection) -> nlohmann::json

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

    using MethodCallback = std::function<nlohmann::json(const nlohmann::json&, Connection& connection)>;

    struct Type {
        const std::string name;
        const std::string description;
        const nlohmann::json data;

        friend std::ostream& operator<<(std::ostream& os, const Type& type);
    };

    struct Error : Type {
        friend std::ostream& operator<<(std::ostream& os, const Error& error);
    };

    struct Method {
        const std::string name;
        const std::string description;
        const nlohmann::json parameters;
        const nlohmann::json returnValue;
        const MethodCallback callback;

        friend std::ostream& operator<<(std::ostream& os, const Method& method);
    };

    class Interface {
    private:
        std::string ifname;
        std::string documentation;
        std::string description;

        std::vector<Type> types;
        std::vector<Method> methods;
        std::vector<Error> errors;

        template<typename Rule>
        struct inserter {};

        struct {
            std::string moving_docstring {};
            std::string docstring {};
            std::string name {};
            std::map<std::string, MethodCallback> callbacks {};
            nlohmann::json method_params {};
        } state;

        struct State {
            std::vector<std::string> fields {};
            size_t pos { 0 };
            nlohmann::json last_type {};
            nlohmann::json last_element_type {};
            nlohmann::json work {};
            bool maybe_type { false };
            bool dict_type { false };
            bool array_type { false };
        };
        std::vector<State> stack;

    public:
        explicit Interface(std::string fromDescription,
                           std::map<std::string, MethodCallback> callbacks = {});
        [[nodiscard]] const std::string& name() const noexcept { return ifname; }
        [[nodiscard]] const Method& method(const std::string& name) const;

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const nlohmann::json& type, size_t indent = 0);
    std::string vtype_to_string(const nlohmann::json& type);

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
        std::vector<Interface> interfaces;
        std::thread listeningThread;
        int listen_fd { -1 };

        nlohmann::json handle(const nlohmann::json &message, Connection &connection);
        void dispatchConnections();
    public:
        Service(std::string address, Description desc);
        Service(const Service& src) = delete;
        Service(Service&& src) noexcept;
        ~Service();

        [[nodiscard]] Connection nextClientConnection() const;
        void addInterface(Interface interface) { interfaces.push_back(std::move(interface)); }
    };

    inline nlohmann::json reply(nlohmann::json params) {
        return {{"parameters", std::move(params)}};
    }
    inline nlohmann::json reply_continues(nlohmann::json params, bool continues = true) {
        return {{"parameters", std::move(params)}, {"continues", continues}};
    }
    inline nlohmann::json error(std::string what, nlohmann::json params) {
        return {{"error", std::move(what)}, {"parameters", std::move(params)}};
    }

    inline void to_json(nlohmann::json& j, const Service::Description& desc) {
        j = nlohmann::json{
                {"vendor", desc.vendor},
                {"product", desc.product},
                {"version", desc.version},
                {"url", desc.url}
        };
    }

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
