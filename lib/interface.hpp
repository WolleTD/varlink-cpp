#ifndef LIBVARLINK_INTERFACE_H
#define LIBVARLINK_INTERFACE_H

#include <string>
#include <vector>
#include <varlink.hpp>
#include <nlohmann/json.hpp>

namespace varlink {
    struct _type {
        nlohmann::json type;
        bool maybe { false };
        bool array { false };
        bool dict { false };
    };

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
            nlohmann::json method_params;
        } state;

        struct State {
            std::vector<std::string> fields;
            size_t pos { 0 };
            nlohmann::json last_type;
            nlohmann::json last_element_type;
            nlohmann::json work;
            bool maybe_type { false };
            bool dict_type { false };
            bool array_type { false };

            explicit State(size_t position) : pos(position) { }
        };
        std::vector<State> stack;

    public:
        explicit Interface(std::string fromDescription);

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const nlohmann::json& type, size_t indent = 0);
    std::string vtype_to_string(const nlohmann::json& type);
}

#endif //LIBVARLINK_INTERFACE_H
