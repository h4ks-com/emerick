#pragma once

#include <QMetaEnum>
#include <QObject>
#include <cctype>
#include <cstdint>
#include <string>

namespace emerick::app {

Q_NAMESPACE

enum class SettingType : std::uint8_t { Integer, Number, String, Boolean, Color, Choice };
Q_ENUM_NS(SettingType)

enum class EntryKind : std::uint8_t { Action, Setting };
Q_ENUM_NS(EntryKind)

enum class Page : std::uint8_t { Main, Settings };
Q_ENUM_NS(Page)

// QML needs capitalized enum keys, so we lower the first letter for ids and JSON.
template <class Enum>
std::string enumKey(Enum value) {
    std::string key = QMetaEnum::fromType<Enum>().valueToKey(static_cast<quint64>(value));
    key.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(key.front())));
    return key;
}

template <class Enum>
std::string enumTitle(Enum value) {
    return QMetaEnum::fromType<Enum>().valueToKey(static_cast<quint64>(value));
}

}  // namespace emerick::app
