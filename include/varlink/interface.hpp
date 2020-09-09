/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_INTERFACE_HPP
#define LIBVARLINK_VARLINK_INTERFACE_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "varlink/common.hpp"

namespace varlink {

    struct Type {
        const std::string name;
        const std::string description;
        const json data;

        friend std::ostream& operator<<(std::ostream& os, const Type& type);
    };

    struct Error : Type {
        friend std::ostream& operator<<(std::ostream& os, const Error& error);
    };

    struct Method {
        const std::string name;
        const std::string description;
        const json parameters;
        const json returnValue;
        const MethodCallback callback;

        friend std::ostream& operator<<(std::ostream& os, const Method& method);
    };

    class Interface {
    private:
        std::string ifname;
        std::string documentation;
        std::string_view description;

        std::vector<Type> types;
        std::vector<Method> methods;
        std::vector<Error> errors;

        template<typename Rule>
        struct inserter {};

        struct {
            std::string moving_docstring {};
            std::string docstring {};
            std::string name {};
            CallbackMap callbacks {};
            json method_params {};
        } state;

        struct State {
            std::vector<std::string> fields {};
            size_t pos { 0 };
            json last_type {};
            json last_element_type {};
            json work {};
            bool maybe_type { false };
            bool dict_type { false };
            bool array_type { false };
        };
        std::vector<State> stack;

    public:
        explicit Interface(std::string_view fromDescription,
                           CallbackMap callbacks = {});
        [[nodiscard]] const std::string& name() const noexcept { return ifname; }
        [[nodiscard]] const std::string& doc() const noexcept { return documentation; }
        [[nodiscard]] const Method& method(const std::string& name) const;
        [[nodiscard]] const Type& type(const std::string& name) const;
        [[nodiscard]] const Error& error(const std::string& name) const;
        [[nodiscard]] bool has_method(const std::string& name) const noexcept;
        [[nodiscard]] bool has_type(const std::string& name) const noexcept;
        [[nodiscard]] bool has_error(const std::string& name) const noexcept;
        void validate(const json& data, const json& type) const;
        json call(const std::string &methodname, const json &message, const SendMore &sendmore) const;

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0);
}

#endif // LIBVARLINK_VARLINK_INTERFACE_HPP
