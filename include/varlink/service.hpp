#ifndef LIBVARLINK_SERVICE_HPP
#define LIBVARLINK_SERVICE_HPP

#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>
#include <varlink/interface.hpp>

namespace varlink {

using more_handler = std::function<void(std::error_code)>;
using reply_function = std::function<void(json, more_handler&&)>;

using sync_callback_function = std::function<json::object_t(const json&, callmode)>;
using async_callback_function = std::function<void(const json&, callmode, reply_function&&)>;
using callback_function = std::variant<nullptr_t, sync_callback_function, async_callback_function>;
using callback_map = std::map<std::string, callback_function>;

struct varlink_service {
    struct description {
        std::string vendor{};
        std::string product{};
        std::string version{};
        std::string url{};
    };

    struct interface {
        explicit interface(std::string_view definition, callback_map callbacks = {});

        explicit interface(varlink_interface spec, callback_map callbacks = {});

        auto* operator->() const { return &spec_; }
        auto& operator*() const { return spec_; }

        void add_callback(const std::string& methodname, callback_function fn);

        [[nodiscard]] const callback_function& callback(const std::string& methodname) const;

      private:
        varlink_interface spec_;
        callback_map callbacks_;
    };

    explicit varlink_service(description Description);

    varlink_service(const varlink_service& src) = delete;
    varlink_service& operator=(const varlink_service&) = delete;
    varlink_service(varlink_service&& src) = delete;
    varlink_service& operator=(varlink_service&&) = delete;

    void message_call(const basic_varlink_message& message, reply_function&& replySender) const noexcept;
    void add_interface(interface&& interf);
    void add_interface(varlink_interface&& spec, callback_map&& callbacks = {});
    void add_interface(std::string_view definition, callback_map&& callbacks = {});

  private:
    [[nodiscard]] auto find_interface(std::string_view ifname) const
        -> std::vector<interface>::const_iterator;

    description desc;
    std::vector<interface> interfaces{};
};
} // namespace varlink
#endif // LIBVARLINK_SERVICE_HPP
