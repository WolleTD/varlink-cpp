#ifndef LIBVARLINK_MESSAGE_HPP
#define LIBVARLINK_MESSAGE_HPP

#include <varlink/detail/nl_json.hpp>

namespace varlink {

class varlink_message {
  public:
    enum class callmode {
        basic,
        oneway,
        more,
        upgrade,
    };

  private:
    json _json;
    callmode _mode{callmode::basic};

  public:
    varlink_message() = default;
    explicit varlink_message(const json& msg) : _json(msg)
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
    varlink_message(
        const std::string_view method,
        const json& parameters,
        callmode mode = callmode::basic)
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
    [[nodiscard]] bool more() const { return (_mode == callmode::more); }
    [[nodiscard]] bool oneway() const { return (_mode == callmode::oneway); }
    [[nodiscard]] bool upgrade() const { return (_mode == callmode::upgrade); }

    [[nodiscard]] json parameters() const
    {
        return _json.contains("parameters") ? _json["parameters"] : json::object();
    }
    [[nodiscard]] const json& json_data() const { return _json; }

    [[nodiscard]] std::pair<std::string, std::string> interface_and_method() const
    {
        const auto& fqmethod = _json["method"].get<std::string>();
        const auto dot = fqmethod.rfind('.');
        // When there is no dot at all, both fields contain the same value, but
        // it's an invalid interface name anyway
        return {fqmethod.substr(0, dot), fqmethod.substr(dot + 1)};
    }

    friend bool operator==(const varlink_message& lhs, const varlink_message& rhs) noexcept;
};

inline bool operator==(const varlink_message& lhs, const varlink_message& rhs) noexcept
{
    return (lhs._json == rhs._json);
}

} // namespace varlink
#endif // LIBVARLINK_MESSAGE_HPP
