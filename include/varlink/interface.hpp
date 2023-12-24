#ifndef LIBVARLINK_INTERFACE_HPP
#define LIBVARLINK_INTERFACE_HPP

#include <varlink/detail/member.hpp>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

struct invalid_parameter : std::invalid_argument {
    explicit invalid_parameter(const std::string& what) : std::invalid_argument(what) {}
};

struct varlink_interface {
    using iterator = std::vector<detail::member>::const_iterator;

    explicit varlink_interface(std::string_view description);

    [[nodiscard]] std::string_view name() const noexcept { return ifname; }
    [[nodiscard]] std::string_view doc() const noexcept { return documentation; }

    [[nodiscard]] iterator begin() const noexcept { return members.begin(); }
    [[nodiscard]] iterator end() const noexcept { return members.end(); }

    [[nodiscard]] iterator find_method(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Method);
    }

    [[nodiscard]] iterator find_type(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Type);
    }

    [[nodiscard]] iterator find_error(std::string_view name) const
    {
        return find_member(name, detail::MemberKind::Error);
    }

    [[nodiscard]] const detail::member& method(std::string_view name) const
    {
        return get_member(name, detail::MemberKind::Method);
    }
    [[nodiscard]] const detail::member& type(std::string_view name) const
    {
        return get_member(name, detail::MemberKind::Type);
    }
    [[nodiscard]] const detail::member& error(std::string_view name) const
    {
        return get_member(name, detail::MemberKind::Error);
    }

    void validate(
        const json& data,
        const detail::type_spec& typespec,
        std::string_view name = "<root>",
        bool collection = false) const;

  private:
    [[nodiscard]] iterator find_member(std::string_view name, detail::MemberKind kind) const;
    [[nodiscard]] const detail::member& get_member(std::string_view name, detail::MemberKind kind) const;

    friend std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);

    detail::string_type ifname{};
    detail::string_type documentation{};
    std::vector<detail::member> members{};
};

std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);
} // namespace varlink
#endif // LIBVARLINK_INTERFACE_HPP
