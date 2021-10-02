#ifndef LIBVARLINK_INTERFACE_HPP
#define LIBVARLINK_INTERFACE_HPP

#include <varlink/detail/member.hpp>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

class invalid_parameter : public std::invalid_argument {
  public:
    explicit invalid_parameter(const std::string& what) : std::invalid_argument(what) {}
};

class varlink_interface {
  public:
    explicit varlink_interface(std::string_view description);

    [[nodiscard]] std::string_view name() const noexcept { return ifname; }
    [[nodiscard]] std::string_view doc() const noexcept { return documentation; }

    [[nodiscard]] const detail::member& method(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] const detail::member& type(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] const detail::member& error(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Error);
    }

    [[nodiscard]] bool has_method(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] bool has_type(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] bool has_error(std::string_view name) const noexcept
    {
        return has_member(name, detail::MemberKind::Error);
    }

    void validate(
        const json& data,
        const detail::type_spec& typespec,
        std::string_view name = "<root>",
        bool collection = false) const;

  private:
    detail::string_type ifname{};
    detail::string_type documentation{};
    std::vector<detail::member> members{};

    [[nodiscard]] const detail::member& find_member(std::string_view name, detail::MemberKind kind) const;
    [[nodiscard]] bool has_member(std::string_view name, detail::MemberKind kind) const;

    friend std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);
};

std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface);
} // namespace varlink
#endif // LIBVARLINK_INTERFACE_HPP
