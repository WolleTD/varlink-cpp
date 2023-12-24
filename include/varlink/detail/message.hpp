#ifndef LIBVARLINK_MESSAGE_HPP
#define LIBVARLINK_MESSAGE_HPP

#include <varlink/detail/nl_json.hpp>

namespace varlink {

enum class callmode {
    basic,
    oneway,
    more,
    upgrade,
};

struct basic_varlink_message {
    basic_varlink_message() = default;
    explicit basic_varlink_message(const json& msg) : json_(msg)
    {
        if (!json_.is_object() or !json_.contains("method") or !json_["method"].is_string()
            or (json_.contains("parameters") && !json_["parameters"].is_object())) {
            throw std::invalid_argument("Not a varlink message: " + msg.dump());
        }
        mode_ = (msg.contains("more") && msg["more"].get<bool>())       ? callmode::more
              : (msg.contains("oneway") && msg["oneway"].get<bool>())   ? callmode::oneway
              : (msg.contains("upgrade") && msg["upgrade"].get<bool>()) ? callmode::upgrade
                                                                        : callmode::basic;
    }

    basic_varlink_message(const std::string_view method, const json& parameters)
        : basic_varlink_message(method, parameters, callmode::basic)
    {
    }

    basic_varlink_message(const std::string_view method, const json& parameters, callmode mode)
        : json_({{"method", method}}), mode_(mode)
    {
        if (not parameters.is_null() and not parameters.is_object()) {
            throw std::invalid_argument("parameters is not an object");
        }
        else if (not parameters.empty()) {
            json_["parameters"] = parameters;
        }
        if (mode_ == callmode::oneway) { json_["oneway"] = true; }
        else if (mode_ == callmode::more) {
            json_["more"] = true;
        }
        else if (mode_ == callmode::upgrade) {
            json_["upgrade"] = true;
        }
    }

    [[nodiscard]] auto mode() const { return mode_; }

    [[nodiscard]] json parameters() const
    {
        return json_.contains("parameters") ? json_["parameters"] : json::object();
    }
    [[nodiscard]] const json& json_data() const { return json_; }

    [[nodiscard]] std::string interface() const
    {
        const auto& fqmethod = json_["method"].get<std::string>();
        return fqmethod.substr(0, fqmethod.rfind('.'));
    }

    [[nodiscard]] std::string method() const
    {
        const auto& fqmethod = json_["method"].get<std::string>();
        return fqmethod.substr(fqmethod.rfind('.') + 1);
    }

    friend bool operator==(const basic_varlink_message& lhs, const basic_varlink_message& rhs) noexcept;

  private:
    json json_;
    callmode mode_{callmode::basic};
};

template <callmode CallMode>
struct typed_varlink_message : basic_varlink_message {
    typed_varlink_message(const std::string_view method, const json& parameters)
        : basic_varlink_message(method, parameters, CallMode)
    {
    }
};

using varlink_message = typed_varlink_message<callmode::basic>;
using varlink_message_more = typed_varlink_message<callmode::more>;
using varlink_message_oneway = typed_varlink_message<callmode::oneway>;
using varlink_message_upgrade = typed_varlink_message<callmode::upgrade>;

inline bool operator==(const basic_varlink_message& lhs, const basic_varlink_message& rhs) noexcept
{
    return (lhs.json_ == rhs.json_);
}

inline bool reply_continues(const json& reply)
{
    return reply.contains("continues") and reply["continues"].get<bool>();
}

} // namespace varlink
#endif // LIBVARLINK_MESSAGE_HPP
