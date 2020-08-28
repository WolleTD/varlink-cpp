#ifndef LIBVARLINK_INTERFACE_H
#define LIBVARLINK_INTERFACE_H

#include <string>
#include <vector>
#include <varlink.hpp>
#include <nlohmann/json.hpp>

namespace varlink {
    struct Type {
        std::string name;
        std::string description;
        nlohmann::json data;

        friend std::ostream& operator<<(std::ostream& os, const Type& type);
    };

    struct Error : Type {
        friend std::ostream& operator<<(std::ostream& os, const Error& error);
    };

    struct Method {
        std::string name;
        std::string description;
        nlohmann::json parameters;
        nlohmann::json returnValue;
        MethodCallback callback;

        friend std::ostream& operator<<(std::ostream& os, const Method& method);
    };

    class Interface {
    private:
        std::string name;
        std::string documentation;
        std::string description;

        std::vector<Type> types;
        std::vector<Method> methods;
        std::vector<Error> errors;

        template<typename Rule>
        struct inserter {};
        struct {
            std::string moving_docstring;
            std::string docstring;
            std::string name;
            std::vector<nlohmann::json> stack;
        } state;

    public:
        explicit Interface(std::string fromDescription);

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const nlohmann::json& type);
    std::string vtype_to_string(const nlohmann::json& type);
}

#endif //LIBVARLINK_INTERFACE_H
