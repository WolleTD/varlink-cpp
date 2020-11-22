#ifndef LIBVARLINK_VARLINK_ERROR_HPP
#define LIBVARLINK_VARLINK_ERROR_HPP

#include <varlink/detail/nl_json.hpp>

namespace varlink {
class varlink_error : public std::logic_error {
    json _args;

  public:
    varlink_error(const std::string& what, json args)
        : std::logic_error(what), _args(std::move(args))
    {
    }
    [[nodiscard]] const json& args() const { return _args; }
};
} // namespace varlink
#endif // LIBVARLINK_VARLINK_ERROR_HPP
