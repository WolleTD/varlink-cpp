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

class basic_varlink_message {
  private:
    json _json;
    callmode _mode{callmode::basic};

  public:
    basic_varlink_message() = default;
    explicit basic_varlink_message(const json& msg) : _json(msg)
    {
        if (!_json.is_object() or !_json.contains("method") or !_json["method"].is_string()
            or (_json.contains("parameters") && !_json["parameters"].is_object())) {
            throw std::invalid_argument("Not a varlink message: " + msg.dump());
        }
        _mode = (msg.contains("more") && msg["more"].get<bool>())       ? callmode::more
              : (msg.contains("oneway") && msg["oneway"].get<bool>())   ? callmode::oneway
              : (msg.contains("upgrade") && msg["upgrade"].get<bool>()) ? callmode::upgrade
                                                                        : callmode::basic;
    }

    basic_varlink_message(const std::string_view method, const json& parameters)
        : basic_varlink_message(method, parameters, callmode::basic)
    {
    }

    basic_varlink_message(const std::string_view method, const json& parameters, callmode mode)
        : _json({{"method", method}}), _mode(mode)
    {
        if (not parameters.is_null() and not parameters.is_object()) {
            throw std::invalid_argument("parameters is not an object");
        }
        else if (not parameters.empty()) {
            _json["parameters"] = parameters;
        }
        if (_mode == callmode::oneway) { _json["oneway"] = true; }
        else if (_mode == callmode::more) {
            _json["more"] = true;
        }
        else if (_mode == callmode::upgrade) {
            _json["upgrade"] = true;
        }
    }

    [[nodiscard]] auto mode() const { return _mode; }

    [[nodiscard]] json parameters() const
    {
        return _json.contains("parameters") ? _json["parameters"] : json::object();
    }
    [[nodiscard]] const json& json_data() const { return _json; }

    [[nodiscard]] std::string interface() const
    {
        const auto& fqmethod = _json["method"].get<std::string>();
        return fqmethod.substr(0, fqmethod.rfind('.'));
    }

    [[nodiscard]] std::string method() const
    {
        const auto& fqmethod = _json["method"].get<std::string>();
        return fqmethod.substr(fqmethod.rfind('.') + 1);
    }

    friend bool operator==(const basic_varlink_message& lhs, const basic_varlink_message& rhs) noexcept;
};

template <callmode CallMode>
class typed_varlink_message : public basic_varlink_message {
  public:
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
    return (lhs._json == rhs._json);
}

inline bool reply_continues(const json& reply)
{
    return reply.contains("continues") and reply["continues"].get<bool>();
}

} // namespace varlink
#endif // LIBVARLINK_MESSAGE_HPP
