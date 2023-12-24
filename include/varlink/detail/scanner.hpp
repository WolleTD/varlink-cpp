#ifndef LIBVARLINK_SCANNER_HPP
#define LIBVARLINK_SCANNER_HPP

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <varlink/detail/member.hpp>
#include <varlink/detail/nl_json.hpp>

#ifdef VARLINK_DISABLE_CTRE
#include <regex>
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <ctre.hpp>
#pragma GCC diagnostic pop
#endif

namespace varlink::detail {
#ifdef VARLINK_DISABLE_CTRE
#define REGEX std::regex
#define expect(...) expect_re(__VA_ARGS__)
#else
#define REGEX static constexpr ctll::fixed_string
#define expect(...) expect_re<__VA_ARGS__>()
#endif

struct scanner {
    explicit scanner(std::string_view description) : desc_(description), pos_(desc_.begin()) {}

    auto read_interface_name()
    {
        expect_keyword("interface");
        return expect(ifname_pattern);
    }

    member read_member()
    {
        member m{};
        if (auto keyword = expect(keyword_pattern); keyword == "type") {
            m.kind = MemberKind::Type;
        }
        else if (keyword == "error") {
            m.kind = MemberKind::Error;
        }
        else if (keyword == "method") {
            m.kind = MemberKind::Method;
        }
        else if (not keyword.empty()) {
            throw std::runtime_error("Unexpected keyword " + std::string(keyword));
        }
        m.name = expect(member_pattern);
        m.description = get_docstring();
        m.data = (m.kind == MemberKind::Method) ? read_method_spec() : read_type_spec();
        return m;
    }

    void expect_keyword(std::string_view keyword)
    {
        if (expect(keyword_pattern) != keyword) {
            throw std::runtime_error("Expected " + std::string(keyword));
        }
    }

    std::string_view get_docstring() { return std::exchange(current_doc_, {}); }

  private:
    type_def read_struct(bool readFirstBracket = true) // NOLINT(misc-no-recursion)
    {
        if (readFirstBracket) expect_keyword("(");
        try {
            expect(closing_bracket);
            return vl_struct{};
        }
        catch (...) {
        }
        type_def fields;
        std::string_view keyword;
        while (keyword != ")") {
            auto name = expect(identifier_pattern);
            keyword = expect(keyword_pattern);
            if (std::holds_alternative<vl_invalid>(fields)) {
                if (keyword == ":") { fields = vl_struct{}; }
                else if (keyword == ",") {
                    fields = vl_enum{};
                }
                else {
                    throw std::runtime_error("Expected : or , got " + std::string(keyword));
                }
            }
            if (auto* s = std::get_if<vl_struct>(&fields); s and keyword == ":") {
                s->emplace_back(name, read_type_spec());
                keyword = expect(keyword_pattern);
            }
            else if (auto* e = std::get_if<vl_enum>(&fields);
                     e and (keyword == "," or keyword == ")")) {
                e->push_back(string_type{name});
            }
            else {
                throw std::runtime_error("Parser error, last keyword: " + std::string(keyword));
            }
        }
        return fields;
    }

    type_spec read_method_spec()
    {
        vl_struct method_types;
        method_types.emplace_back("parameters", type_spec{read_struct()});
        expect_keyword("->");
        method_types.emplace_back("return_value", type_spec{read_struct()});
        return type_spec{std::move(method_types)};
    }

    type_spec read_type_spec(bool wasMaybe = false) // NOLINT(misc-no-recursion)
    {
        if (auto keyword = expect(keyword_pattern, member_pattern); keyword == "?") {
            if (wasMaybe) throw std::runtime_error("Double '?'");
            auto type = read_type_spec(true);
            type.maybe_type = true;
            return type;
        }
        else if (keyword == "[string]") {
            auto type = read_type_spec();
            type.dict_type = true;
            return type;
        }
        else if (keyword == "[]") {
            auto type = read_type_spec();
            type.array_type = true;
            return type;
        }
        else if (keyword == "(") {
            return type_spec{read_struct(false)};
        }
        else {
            return type_spec{string_type{keyword}};
        }
    }

#ifdef VARLINK_DISABLE_CTRE
    template <typename... Regex>
    std::string_view expect_re(const Regex&... rex)
    {
        static_assert((std::is_same_v<std::regex, Regex> && ...));
        if (std::cmatch m;
            std::regex_search(pos_, desc_.end(), m, whitespace) && m.prefix().length() == 0) {
            auto s = m.str();
            for (auto it = std::cregex_iterator(m[0].first, m[0].second, docstring);
                 it != std::cregex_iterator{};
                 ++it) {
                auto& doc = (*it)[0];
                if (current_doc_.empty())
                    current_doc_ = std::string_view(doc.first, static_cast<size_t>(doc.length()));
                else {
                    auto new_len = static_cast<size_t>(doc.first - current_doc_.data() + doc.length());
                    current_doc_ = std::string_view(current_doc_.data(), new_len);
                }
            }
            pos_ += m.length();
        }
        if (pos_ == desc_.end()) return {};
        std::cmatch m, _m;
        auto match = [this, &m, &_m](auto& re) {
            if (std::regex_search(pos_, desc_.end(), _m, re) && _m.prefix().length() == 0) {
                m = _m;
            }
        };
        (match(rex), ...);
        if (not m.empty()) {
            pos_ += m.length();
            return {m[0].first, static_cast<size_t>(m[0].length())};
        }
        else {
            throw std::runtime_error("interface error" + _m.str() + " | " + _m.prefix().str());
        }
    }
#else
    template <CTRE_REGEX_INPUT_TYPE Regex>
    constexpr void match_one(std::string_view& out)
    {
        if (auto m = ctre::starts_with<Regex>(pos_, desc_.end()); m && out.empty()) {
            out = m.to_view();
        }
    }

    void consume_whitespace()
    {
        if (auto m = ctre::starts_with<whitespace>(pos_, desc_.end()); m) {
            auto s = m.to_view();
            auto doc = ctre::search<docstring>(s);
            while (doc) {
                if (current_doc_.empty())
                    current_doc_ = doc.to_view();
                else {
                    auto new_len = static_cast<size_t>(doc.data() - current_doc_.data()) + doc.size();
                    current_doc_ = std::string_view(current_doc_.data(), new_len);
                }
                s.remove_prefix(doc.size());
                doc = ctre::search<docstring>(s);
            }
            pos_ += static_cast<ptrdiff_t>(m.size());
        }
    }

    template <CTRE_REGEX_INPUT_TYPE... Regex>
    std::string_view expect_re()
    {
        consume_whitespace();
        if (pos_ == desc_.end()) return {};
        std::string_view match;
        (match_one<Regex>(match), ...);
        if (not match.empty()) {
            pos_ += static_cast<ptrdiff_t>(match.length());
            return match;
        }
        else {
            auto end = std::min(desc_.end(), pos_ + 20);
            throw std::runtime_error("interface error " + std::string(pos_, end));
        }
    }
#endif

    const std::string_view desc_;
    std::string_view::const_iterator pos_;
    std::string_view current_doc_;

    REGEX whitespace{"([ \t\n]|#[^\n]*(\n|$))+"};
    REGEX docstring{"(?:[^\n]*)#([^\n]*)\r?\n"};
    REGEX keyword_pattern{R"(\b[a-z]+\b|[:,(){}]|->|\[\]|\?|\[string\])"};
    REGEX closing_bracket{"\\)"};
    REGEX ifname_pattern{"[a-z](-*[a-z0-9])*(\\.[a-z0-9](-*[a-z0-9])*)+"};
    REGEX member_pattern{"\\b[A-Z][A-Za-z0-9]*\\b"};
    REGEX identifier_pattern{"\\b[A-Za-z](_?[A-Za-z0-9])*\\b"};
};
}

#undef REGEX
#undef expect
#endif // LIBVARLINK_SCANNER_HPP
