#ifndef LIBVARLINK_VARLINK_ERROR_HPP
#define LIBVARLINK_VARLINK_ERROR_HPP

#include <string>
#include <system_error>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

class varlink_error_category : public std::error_category {
  private:
    std::vector<std::string> errors{"no_error"};

  public:
    [[nodiscard]] const char* name() const noexcept final { return "VarlinkError"; }

    [[nodiscard]] std::string message(int c) const final
    {
        try {
            return errors.at(static_cast<size_t>(c));
        }
        catch (std::out_of_range& e) {
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
};

class varlink_error : public std::logic_error {
    json _args;

  public:
    varlink_error(const std::string& what, json args)
        : std::logic_error(what), _args(std::move(args))
    {
    }
    [[nodiscard]] const json& args() const { return _args; }
};

extern inline varlink::varlink_error_category& varlink_category()
{
    static varlink::varlink_error_category c;
    return c;
}

inline std::error_code make_varlink_error(std::string_view varlink_error)
{
    return varlink_category().get_error_code(varlink_error);
}
} // namespace varlink

#endif // LIBVARLINK_VARLINK_ERROR_HPP
