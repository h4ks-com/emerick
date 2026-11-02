#include "emerick/app/variant.hpp"

#include <QJsonArray>
#include <QJsonDocument>

namespace emerick::app {

QString qs(const std::string& text) { return QString::fromStdString(text); }

QVariant toVariant(const Json& value) {
    const auto wrapped = QJsonDocument::fromJson(QByteArray::fromStdString(Json::array({value}).dump()));
    return wrapped.array().at(0).toVariant();
}

Json toJson(const QVariant& value) {
    const QJsonDocument wrapped{QJsonArray{QJsonValue::fromVariant(value)}};
    return Json::parse(wrapped.toJson(QJsonDocument::Compact).toStdString()).at(0);
}

}  // namespace emerick::app
