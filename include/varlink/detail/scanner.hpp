#ifndef LIBVARLINK_SCANNER_HPP
#define LIBVARLINK_SCANNER_HPP

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>
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
using reply_function = std::function<void(json::object_t, bool)>;
using callback_function = std::function<void(const json&, bool, const reply_function&)>;

#ifdef VARLINK_DISABLE_CTRE
#define REGEX std::regex
#define expect(...) expect_re(__VA_ARGS__)
#else
#define REGEX static constexpr ctll::fixed_string
#define expect(...) expect_re<__VA_ARGS__>()
#endif

enum class MemberKind { Undefined, Type, Error, Method };
struct member {
    MemberKind kind{MemberKind::Undefined};
    std::string name;
    std::string description;
    json data;

    member() = default;
    member(MemberKind kind_, std::string name_, std::string desc, json data_)
        : kind(kind_), name(std::move(name_)), description(std::move(desc)), data(std::move(data_))
    {
    }

    explicit operator bool() const { return kind != MemberKind::Undefined; }
    friend std::ostream& operator<<(std::ostream& os, const member& member);
};

class scanner {
  public:
    explicit scanner(std::string description) : desc_(std::move(description)), pos_(desc_.begin())
    {
    }

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
            m.name = expect(member_pattern);
            m.description = get_docstring();
            m.data = read_type_spec()["type"];
        }
        else if (keyword == "error") {
            m.kind = MemberKind::Error;
            m.name = expect(member_pattern);
            m.description = get_docstring();
            m.data = read_type_spec()["type"];
        }
        else if (keyword == "method") {
            m.kind = MemberKind::Method;
            m.name = expect(member_pattern);
            m.data["parameters"] = read_struct();
            expect_keyword("->");
            m.data["return_value"] = read_struct();
            m.description = get_docstring();
        }
        else if (not keyword.empty()) {
            throw std::runtime_error("Unexpected keyword " + keyword);
        }
        return m;
    }

    void expect_keyword(const std::string& keyword)
    {
        if (expect(keyword_pattern) != keyword) { throw std::runtime_error("Expected " + keyword); }
    }

    auto expect_member_name() { return expect(member_pattern); }
    auto expect_identifier() { return expect(identifier_pattern); }

    std::string get_docstring() { return std::exchange(current_doc_, {}); }

  private:
    json read_struct(bool readFirstBracket = true)
    {
        if (readFirstBracket) expect_keyword("(");
        json fields;
        std::optional<bool> isEnum = std::nullopt;
        std::string keyword;
        try {
            keyword = expect(closing_bracket);
        }
        catch (...) {
        }
        while (keyword != ")") {
            auto name = expect_identifier();
            keyword = expect(keyword_pattern);
            if (not isEnum.has_value()) {
                if (keyword == ":") {
                    isEnum = false;
                    fields = json::object();
                    fields["_order"] = json::array();
                }
                else if (keyword == ",") {
                    isEnum = true;
                    fields = json::array();
                }
                else {
                    throw std::runtime_error("Expected : or , got " + keyword);
                }
            }
            if (not isEnum.value() and keyword == ":") {
                fields[name] = read_type_spec();
                fields["_order"].push_back(name);
                keyword = expect(keyword_pattern);
            }
            else if (isEnum.value() and (keyword == "," or keyword == ")")) {
                fields.push_back(name);
            }
        }
        return fields;
    }

    json read_type_spec(bool wasMaybe = false)
    {
        if (auto keyword = expect(keyword_pattern, member_pattern); keyword == "?") {
            if (wasMaybe) throw std::runtime_error("Double '?'");
            auto type = read_type_spec(true);
            type["maybe_type"] = true;
            return type;
        }
        else if (keyword == "[string]") {
            auto type = read_type_spec();
            type["dict_type"] = true;
            return type;
        }
        else if (keyword == "[]") {
            auto type = read_type_spec();
            type["array_type"] = true;
            return type;
        }
        else if (keyword == "(") {
            return json{{"type", read_struct(false)}};
        }
        else {
            return json{{"type", keyword}};
        }
    }

#ifdef VARLINK_DISABLE_CTRE
    template <typename... Regex>
    std::string expect_re(const Regex&... rex)
    {
        static_assert((std::is_same_v<std::regex, Regex> && ...));
        if (std::smatch m;
            std::regex_search(pos_, desc_.end(), m, whitespace) && m.prefix().length() == 0) {
            auto s = m.str();
            for (auto it = std::sregex_iterator(s.begin(), s.end(), docstring);
                 it != std::sregex_iterator{};
                 ++it) {
                current_doc_ += "#" + (*it)[1].str() + "\n";
            }
            pos_ += m.length();
        }
        if (pos_ == desc_.end()) return {};
        std::smatch m, _m;
        auto match = [this, &m, &_m](auto& re) {
            if (std::regex_search(pos_, desc_.end(), _m, re) && _m.prefix().length() == 0) {
                m = _m;
            }
        };
        (match(rex), ...);
        if (not m.empty()) {
            pos_ += m.length();
            return m.str();
        }
        else {
            throw std::runtime_error("interface error" + _m.str() + " | " + _m.prefix().str());
        }
    }
#else
    template <CTRE_REGEX_INPUT_TYPE Regex>
    auto match_one(std::string_view& out)
    {
        if (auto m = ctre::starts_with<Regex>(pos_, desc_.end()); m && out.empty()) {
            out = m.to_view();
        }
    }

    template <CTRE_REGEX_INPUT_TYPE... Regex>
    std::string expect_re()
    {
        if (auto m = ctre::starts_with<whitespace>(pos_, desc_.end()); m) {
            auto s = m.to_view();
            auto doc = ctre::search<docstring>(s);
            while (doc) {
                current_doc_ += doc.to_string();
                s.remove_prefix(doc.size());
                doc = ctre::search<docstring>(s);
            }
            pos_ += static_cast<ptrdiff_t>(m.size());
        }
        if (pos_ == desc_.end()) return {};
        std::string_view match;
        (match_one<Regex>(match), ...);
        if (not match.empty()) {
            pos_ += static_cast<ptrdiff_t>(match.length());
            return std::string(match);
        }
        else {
            throw std::runtime_error("interface error" + std::string(pos_, pos_ + 20));
        }
    }
#endif

    const std::string desc_;
    std::string::const_iterator pos_;
    std::string current_doc_;

    REGEX whitespace{"([ \t\n]|#[^\n]*(\n|$))+"};
    REGEX docstring{"(?:[^\n]*)#([^\n]*)\r?\n"};
    REGEX method_signature{
        "([ \t\n]|#[^\n]*\n)*(\\([^)]*\\))([ \t\n]|#[^\n]*\n)*->([ \t\n]|#[^\n]*\n)*(\\([^)]*\\))"};
    REGEX keyword_pattern{R"(\b[a-z]+\b|[:,(){}]|->|\[\]|\?|\[string\])"};
    REGEX closing_bracket{"\\)"};
    REGEX ifname_pattern{"[a-z](-*[a-z0-9])*(\\.[a-z0-9](-*[a-z0-9])*)+"};
    REGEX member_pattern{"\\b[A-Z][A-Za-z0-9]*\\b"};
    REGEX identifier_pattern{"\\b[A-Za-z](_?[A-Za-z0-9])*\\b"};
};

inline std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0)
{
    if (elem.is_string()) { return elem.get<std::string>(); }
    else {
        const auto is_multiline = [&]() -> bool {
            if (indent < 0) return false;
            if (elem.is_null() || elem.empty()) return false;
            if (elem.is_object()) {
                if (elem["_order"].size() > 2) return true;
                for (const auto& member_name : elem["_order"]) {
                    auto& member = elem[member_name.get<std::string>()];
                    if (member.contains("array_type") && member["array_type"].get<bool>())
                        return true;
                    if (member["type"].is_object()) return true;
                }
            }
            if (element_to_string(elem, -1, depth).size() > 40) return true;
            return false;
        };
        const bool multiline{is_multiline()};
        const std::string spaces = multiline
                                     ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ')
                                     : "";
        const std::string sep = multiline ? ",\n" : ", ";
        std::string s = multiline ? "(\n" : "(";
        bool first = true;
        if (elem.is_null()) { return "()"; }
        else if (elem.is_array()) {
            for (const auto& member : elem) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + member.get<std::string>();
            }
        }
        else {
            for (const auto& member : elem["_order"]) {
                if (first)
                    first = false;
                else
                    s += sep;
                s += spaces + member.get<std::string>() + ": ";
                auto type = elem[member.get<std::string>()];
                if (type.contains("maybe_type") && type["maybe_type"].get<bool>()) s += "?";
                if (type.contains("array_type") && type["array_type"].get<bool>()) s += "[]";
                if (type.contains("dict_type") && type["dict_type"].get<bool>()) s += "[string]";
                s += element_to_string(type["type"], indent, depth + 1);
            }
        }
        s += multiline ? "\n" + std::string(static_cast<size_t>(indent) * depth, ' ') + ")" : ")";
        return s;
    }
}

inline std::ostream& operator<<(std::ostream& os, const member& mem)
{
    try {
        if (mem.kind == MemberKind::Type) {
            os << mem.description << "type " << mem.name << " " << element_to_string(mem.data)
               << "\n";
        }
        else if (mem.kind == MemberKind::Error) {
            os << mem.description << "error " << mem.name << " " << element_to_string(mem.data, -1)
               << "\n";
        }
        else if (mem.kind == MemberKind::Method) {
            os << mem.description << "method " << mem.name
               << element_to_string(mem.data["parameters"], -1) << " -> "
               << element_to_string(mem.data["return_value"]) << "\n";
        }
    }
    catch (...) {
        os << "brokey " << mem.name;
    }
    return os;
}
}

#undef REGEX
#undef expect
#endif // LIBVARLINK_SCANNER_HPP
