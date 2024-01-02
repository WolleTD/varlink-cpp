#ifndef LIBVARLINK_VARLINK_ERROR_HPP
#define LIBVARLINK_VARLINK_ERROR_HPP

#include <string>
#include <system_error>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

struct varlink_error : std::runtime_error {
    varlink_error(std::string type, json params)
        : std::runtime_error(type + " with args: " + params.dump()),
          type_(std::move(type)),
          params_(std::move(params))
    {
    }

    [[nodiscard]] const std::string& type() const noexcept { return type_; }
    [[nodiscard]] const json& params() const noexcept { return params_; }

  private:
    std::string type_;
    json params_;
};
} // namespace varlink

#endif // LIBVARLINK_VARLINK_ERROR_HPP
