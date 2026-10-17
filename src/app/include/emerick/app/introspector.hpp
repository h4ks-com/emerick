#pragma once

#include <QString>
#include <chrono>
#include <optional>

#include "emerick/app/registry.hpp"

namespace emerick::app {

Json uiTree(const Registry& registry, bool namedOnly, const QString& root = {});
Json findItems(const QString& query);
Json describeItem(const QString& objectName);
Json itemProperty(const QString& objectName, const QString& name);
Json setItemProperty(const QString& objectName, const QString& name, const Json& value);
Json invokeItem(const QString& objectName);
Json screenshot(const QString& path);
std::optional<Json> waitForVisibleItem(const QString& objectName, std::chrono::milliseconds timeout);

}  // namespace emerick::app
