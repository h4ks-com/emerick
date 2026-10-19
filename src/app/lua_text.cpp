#include "emerick/app/lua_text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <string_view>

#include "emerick/app/lua_host.hpp"

namespace emerick::app {

namespace {

constexpr std::array<std::string_view, 22> kLuaKeywords{
    "and", "break", "do",  "else", "elseif", "end",    "false",  "for",  "function", "goto",  "if",
    "in",  "local", "nil", "not",  "or",     "repeat", "return", "then", "true",     "until", "while"};

bool isIdentifier(std::string_view text) {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    return !text.empty() && letter(text.front()) &&
           std::ranges::all_of(text, [&](char c) { return letter(c) || digit(c); }) &&
           !std::ranges::contains(kLuaKeywords, text);
}

std::string luaString(std::string_view text) {
    std::string quoted = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':
                quoted += "\\\"";
                break;
            case '\\':
                quoted += "\\\\";
                break;
            case '\n':
                quoted += "\\n";
                break;
            case '\r':
                quoted += "\\r";
                break;
            case '\t':
                quoted += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < ' ') {
                    quoted += std::format("\\{:03d}", static_cast<int>(c));
                } else {
                    quoted += c;
                }
        }
    }
    return quoted + '"';
}

std::string luaKey(const std::string& key) { return isIdentifier(key) ? key : "[" + luaString(key) + "]"; }

std::string joined(const std::vector<std::string>& parts, std::string_view separator) {
    std::string text;
    for (const std::string& part : parts) {
        text += (text.empty() ? "" : std::string{separator}) + part;
    }
    return text;
}

const LuaMember* member(std::string_view name) {
    const std::string_view bare =
        name.starts_with("emerick.") ? name.substr(std::string_view{"emerick."}.size()) : name;
    const auto found = std::ranges::find(luaMembers(), bare, &LuaMember::name);
    return found == luaMembers().end() ? nullptr : &*found;
}

std::string memberHelp(const LuaMember& entry) {
    return "emerick." + std::string{entry.signature} + "\n  " + std::string{entry.summary};
}

std::string actionHelp(const Action& entry) {
    std::string text = entry.title + "  (" + entry.id + ")";
    if (!entry.description.empty()) {
        text += "\n" + entry.description;
    }
    text += "\nParameters: " + paramSummary(entry.schema);
    text += "\nShortcut: " + (entry.shortcut.empty() ? std::string{"none"} : entry.shortcut);
    return text;
}

std::string settingHelp(const Registry& registry, const Setting& entry) {
    const Json described = registry.describeSetting(entry.id);
    std::string text = entry.title + "  (" + entry.id + ")";
    text += "\nType: " + described.at("type").get<std::string>();
    if (!entry.choices.empty()) {
        text += ", one of " + entry.choices.dump();
    }
    text += "\nValue: " + described.at("value").dump();
    if (!entry.hint.empty()) {
        text += "\n" + entry.hint;
    }
    return text;
}

}  // namespace

std::string luaLiteral(const Json& value) {
    switch (value.type()) {
        case Json::value_t::string:
            return luaString(value.get<std::string>());
        case Json::value_t::array:
            return "{" +
                   joined(value | std::views::transform([](const Json& item) { return luaLiteral(item); }) |
                              std::ranges::to<std::vector>(),
                          ", ") +
                   "}";
        case Json::value_t::object: {
            std::vector<std::string> fields;
            for (const auto& [key, item] : value.items()) {
                fields.push_back(luaKey(key) + "=" + luaLiteral(item));
            }
            return "{" + joined(fields, ", ") + "}";
        }
        case Json::value_t::null:
        case Json::value_t::discarded:
            return "nil";
        default:
            return value.dump();
    }
}

std::string luaCall(const std::string& id, const Json& params) {
    const bool bare = params.is_null() || (params.is_object() && params.empty());
    return "emerick.run(" + luaString(id) + (bare ? "" : ", " + luaLiteral(params)) + ")";
}

std::string paramSummary(const Json& schema) {
    if (!schema.is_object() || !schema.contains("properties") || schema.at("properties").empty()) {
        return "none";
    }
    const auto required = schema.value("required", Json::array()).get<std::vector<std::string>>();
    std::vector<std::string> params;
    for (const auto& [name, property] : schema.at("properties").items()) {
        const bool optional = !std::ranges::contains(required, name);
        params.push_back(name + (optional ? "?" : "") + ": " + property.value("type", std::string{"any"}));
    }
    return joined(params, ", ");
}

std::string helpText(const Registry& registry, const std::string& name) {
    if (name.empty()) {
        return "help(name) describes an action, a setting or one of these functions:\n" +
               joined(luaMembers() | std::views::transform(memberHelp) | std::ranges::to<std::vector>(), "\n");
    }
    if (const LuaMember* entry = member(name)) {
        return memberHelp(*entry);
    }
    if (const Action* entry = registry.action(name)) {
        return actionHelp(*entry);
    }
    if (const Setting* entry = registry.setting(name)) {
        return settingHelp(registry, *entry);
    }
    throw AppError("no action, setting or emerick function " + name + "; run help() to list the functions");
}

}  // namespace emerick::app
