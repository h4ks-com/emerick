#pragma once

#include <QMetaEnum>
#include <QObject>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace emerick::app {

Q_NAMESPACE

enum class SettingType : std::uint8_t { Integer, Number, String, Boolean, Color, Choice, List };
Q_ENUM_NS(SettingType)

enum class FileDialogMode : std::uint8_t { Open, Save, Folder };
Q_ENUM_NS(FileDialogMode)

enum class MenuRow : std::uint8_t { Command, Submenu, Menu };
Q_ENUM_NS(MenuRow)

enum class EntryKind : std::uint8_t { Action, Setting };
Q_ENUM_NS(EntryKind)

enum class Page : std::uint8_t { Main, Settings };
Q_ENUM_NS(Page)

// Who ran an action, as the console's action log shows it.
enum class ActionSource : std::uint8_t { Code, Ui, Mcp, Lua };
Q_ENUM_NS(ActionSource)

// Log lines are the action log: each action anyone ran, written as the Lua call that runs it.
enum class ConsoleLine : std::uint8_t { Input, Print, Result, Error, Log };
Q_ENUM_NS(ConsoleLine)

enum class ConsoleFilter : std::uint8_t { All, Info, Output, Errors };
Q_ENUM_NS(ConsoleFilter)

enum class ScriptScope : std::uint8_t { User, Project };
Q_ENUM_NS(ScriptScope)

// Api is a name in the emerick table; ActionId is the id string an emerick.run, emerick.setting or help call takes.
enum class CodeToken : std::uint8_t { Keyword, StringLiteral, NumberLiteral, Comment, Call, Api, ActionId };
Q_ENUM_NS(CodeToken)
inline constexpr std::size_t kCodeTokens = 7;

// QML needs capitalized enum keys, so we lower the first letter for ids and JSON.
template <class Enum>
std::string enumKey(Enum value) {
    std::string key = QMetaEnum::fromType<Enum>().valueToKey(static_cast<quint64>(value));
    key.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(key.front())));
    return key;
}

template <class Enum>
std::optional<Enum> enumFromKey(std::string key) {
    if (key.empty()) {
        return std::nullopt;
    }
    key.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(key.front())));
    bool found = false;
    const int value = QMetaEnum::fromType<Enum>().keyToValue(key.c_str(), &found);
    return found ? std::optional{static_cast<Enum>(value)} : std::nullopt;
}

template <class Enum>
std::string enumTitle(Enum value) {
    return QMetaEnum::fromType<Enum>().valueToKey(static_cast<quint64>(value));
}

}  // namespace emerick::app
