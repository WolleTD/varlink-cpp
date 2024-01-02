#ifndef LIBVARLINK_VARLINK_ERROR_HPP
#define LIBVARLINK_VARLINK_ERROR_HPP

#include <string>
#include <system_error>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

struct varlink_error_category : std::error_category {
    [[nodiscard]] const char* name() const noexcept final { return "VarlinkError"; }

    [[nodiscard]] std::string message(int c) const final
    {
        try {
            return errors.at(static_cast<size_t>(c));
        }
        catch (std::out_of_range&) {
            return "unknown";
        }
    }

    [[nodiscard]] std::error_code get_error_code(std::string_view error)
    {
        auto err = std::find(errors.begin(), errors.end(), error);
        auto idx = err - errors.begin();
        if (err == errors.end()) errors.emplace_back(error);
        return {static_cast<int>(idx), *this};
    }

  private:
    std::vector<std::string> errors{"no_error"};
};

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

extern inline varlink_error_category& varlink_category()
{
    static varlink_error_category c;
    return c;
}

inline std::error_code make_varlink_error(std::string_view varlink_error)
{
    return varlink_category().get_error_code(varlink_error);
}
} // namespace varlink

#endif // LIBVARLINK_VARLINK_ERROR_HPP
