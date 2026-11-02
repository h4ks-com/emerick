#pragma once

#include <QString>
#include <QVariant>
#include <nlohmann/json.hpp>
#include <string>

namespace emerick::app {

using Json = nlohmann::json;

QString qs(const std::string& text);
QVariant toVariant(const Json& value);
Json toJson(const QVariant& value);

}  // namespace emerick::app
