#ifndef LIBVARLINK_INTERFACE_HPP
#define LIBVARLINK_INTERFACE_HPP

#include <varlink/detail/member.hpp>
#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>

namespace varlink {

class varlink_interface {
  public:
    explicit varlink_interface(std::string_view description);

    [[nodiscard]] const std::string& name() const noexcept { return ifname; }
    [[nodiscard]] const std::string& doc() const noexcept { return documentation; }

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
    std::string ifname{};
    std::string documentation{};
    std::string_view description{};
    std::vector<detail::member> members{};

    [[nodiscard]] const detail::member& find_member(std::string_view name, detail::MemberKind kind) const
    {
        auto i = std::find_if(members.begin(), members.end(), [&](const auto& e) {
            return e.name == name && (e.kind == kind || kind == detail::MemberKind::Undefined);
        });
        if (i == members.end()) throw std::out_of_range(std::string(name));
        return *i;
    }

    [[nodiscard]] bool has_member(std::string_view name, detail::MemberKind kind) const
    {
        return std::any_of(members.begin(), members.end(), [&](const auto& e) {
            return e.name == name && (e.kind == kind || kind == detail::MemberKind::Undefined);
        });
    }

    friend std::ostream& operator<<(std::ostream& os, const varlink_interface& interface);
};

std::ostream& operator<<(std::ostream& os, const varlink::varlink_interface& interface);
} // namespace varlink
#endif // LIBVARLINK_INTERFACE_HPP
