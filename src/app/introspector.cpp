#include "emerick/app/introspector.hpp"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <array>

#include "emerick/app/event_wait.hpp"

namespace emerick::app {

namespace {

constexpr std::array kReadProperties{"text", "title", "checked", "value", "currentIndex"};
constexpr std::array kActionMethods{"click()", "toggle()", "trigger()", "open()"};

std::vector<QQuickWindow*> windows() {
    std::vector<QQuickWindow*> result;
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto* quick = qobject_cast<QQuickWindow*>(window)) {
            result.push_back(quick);
        }
    }
    return result;
}

void collect(QQuickItem* item, std::vector<QQuickItem*>& out) {
    out.push_back(item);
    for (QQuickItem* child : item->childItems()) {
        collect(child, out);
    }
}

std::vector<QQuickItem*> allItems() {
    std::vector<QQuickItem*> result;
    for (QQuickWindow* window : windows()) {
        collect(window->contentItem(), result);
    }
    return result;
}

QString cleanType(const QObject* object) {
    QString name = QString::fromLatin1(object->metaObject()->className());
    if (const auto generated = name.indexOf("_QML"); generated > 0) {
        name.truncate(generated);
    }
    return name.startsWith("QQuick") ? name.mid(6) : name;
}

Json rect(const QRectF& geometry) {
    return {{"x", geometry.x()}, {"y", geometry.y()}, {"width", geometry.width()}, {"height", geometry.height()}};
}

Json node(const QQuickItem* item) {
    Json result{{"type", cleanType(item).toStdString()},
                {"visible", item->isVisible()},
                {"enabled", item->isEnabled()},
                {"rect", rect(item->mapRectToScene(item->boundingRect()))}};
    if (!item->objectName().isEmpty()) {
        result["objectName"] = item->objectName().toStdString();
    }
    for (const char* name : kReadProperties) {
        if (item->metaObject()->indexOfProperty(name) >= 0) {
            result[name] = toJson(item->property(name));
        }
    }
    return result;
}

Json subtree(QQuickItem* item, bool namedOnly) {
    Json children = Json::array();
    for (QQuickItem* child : item->childItems()) {
        Json below = subtree(child, namedOnly);
        if (below.is_array()) {
            children.insert(children.end(), below.begin(), below.end());
        } else {
            children.push_back(std::move(below));
        }
    }
    if (namedOnly && item->objectName().isEmpty()) {
        return children;
    }
    Json result = node(item);
    if (!children.empty()) {
        result["children"] = std::move(children);
    }
    return result;
}

bool textContains(const QVariant& value, const QString& query) {
    return value.typeId() == QMetaType::QString && value.toString().contains(query, Qt::CaseInsensitive);
}

QQuickItem* item(const QString& objectName) {
    const auto items = allItems();
    const auto visible = std::ranges::find_if(items, [&](const QQuickItem* candidate) {
        return candidate->objectName() == objectName && candidate->isVisible();
    });
    if (visible != items.end()) {
        return *visible;
    }
    const auto any =
        std::ranges::find_if(items, [&](const QQuickItem* candidate) { return candidate->objectName() == objectName; });
    if (any == items.end()) {
        throw AppError("no UI item " + objectName.toStdString() + ", reveal it first");
    }
    return *any;
}

}  // namespace

Json uiTree(const Registry& registry, bool namedOnly, const QString& root) {
    Json result = Json::object();
    if (root.isEmpty()) {
        result["windows"] = Json::array();
        for (QQuickWindow* window : windows()) {
            Json entry{{"type", "Window"},
                       {"title", window->title().toStdString()},
                       {"visible", window->isVisible()},
                       {"rect", rect(window->geometry())}};
            if (!window->objectName().isEmpty()) {
                entry["objectName"] = window->objectName().toStdString();
            }
            Json content = subtree(window->contentItem(), namedOnly);
            entry["children"] = content.is_array() ? content : Json::array({content});
            result["windows"].push_back(std::move(entry));
        }
    } else {
        result["root"] = subtree(item(root), false);
    }

    const auto items = allItems();
    const auto revealed = [&](const std::string& id) {
        return std::ranges::any_of(items, [&](const QQuickItem* candidate) {
            return candidate->isVisible() && candidate->objectName().toStdString() == id;
        });
    };
    result["unrevealed"] = Json::array();
    const auto addUnrevealed = [&](const auto& entry) {
        if (!entry.location.empty() && !revealed(entry.id)) {
            result["unrevealed"].push_back({{"id", entry.id}, {"title", entry.title}, {"location", entry.location}});
        }
    };
    std::ranges::for_each(registry.actions(), addUnrevealed);
    std::ranges::for_each(registry.allSettings(), addUnrevealed);
    return result;
}

Json findItems(const QString& query) {
    Json result = Json::array();
    for (const QQuickItem* candidate : allItems()) {
        if (candidate->objectName().contains(query, Qt::CaseInsensitive) ||
            textContains(candidate->property("text"), query) || textContains(candidate->property("title"), query)) {
            result.push_back(node(candidate));
        }
    }
    return result;
}

Json describeItem(const QString& objectName) { return node(item(objectName)); }

Json itemProperty(const QString& objectName, const QString& name) {
    const QQuickItem* target = item(objectName);
    if (target->metaObject()->indexOfProperty(name.toUtf8().constData()) < 0) {
        throw AppError(objectName.toStdString() + " has no property " + name.toStdString());
    }
    return toJson(target->property(name.toUtf8().constData()));
}

Json setItemProperty(const QString& objectName, const QString& name, const Json& value) {
    QQuickItem* target = item(objectName);
    const int index = target->metaObject()->indexOfProperty(name.toUtf8().constData());
    if (index < 0 || !target->metaObject()->property(index).isWritable()) {
        throw AppError(objectName.toStdString() + " has no writable property " + name.toStdString());
    }
    if (!target->metaObject()->property(index).write(target, toVariant(value))) {
        throw AppError("cannot write " + value.dump() + " to " + objectName.toStdString() + "." + name.toStdString());
    }
    return node(target);
}

Json invokeItem(const QString& objectName) {
    QQuickItem* target = item(objectName);
    if (!target->isEnabled() || !target->isVisible()) {
        throw AppError(objectName.toStdString() + " is disabled or hidden");
    }
    for (const char* signature : kActionMethods) {
        const int index = target->metaObject()->indexOfMethod(signature);
        if (index >= 0) {
            target->metaObject()->method(index).invoke(target);
            return node(target);
        }
    }
    // A picker has no click method, so we open its list the way a click does.
    if (auto* popup = target->property("popup").value<QObject*>()) {
        QMetaObject::invokeMethod(popup, "open");
        return node(target);
    }
    throw AppError(objectName.toStdString() + " has no click, toggle, trigger, open or popup");
}

Json screenshot(const QString& path) {
    const auto all = windows();
    const auto shown = std::ranges::find_if(all, &QWindow::isVisible);
    if (shown == all.end()) {
        throw AppError("no visible window to grab");
    }
    const QString file =
        path.isEmpty()
            ? QDir::temp().filePath(QStringLiteral("emerick-%1.png").arg(QDateTime::currentMSecsSinceEpoch()))
            : path;
    const QImage image = (*shown)->grabWindow();
    if (!image.save(file, "PNG")) {
        throw AppError("cannot write " + file.toStdString());
    }
    return {{"path", file.toStdString()}, {"width", image.width()}, {"height", image.height()}};
}

std::optional<Json> waitForVisibleItem(const QString& objectName, std::chrono::milliseconds timeout) {
    const QQuickItem* found = nullptr;
    processEventsUntil(
        [&] {
            const auto items = allItems();
            const auto shown = std::ranges::find_if(items, [&](const QQuickItem* candidate) {
                return candidate->objectName() == objectName && candidate->isVisible() &&
                       candidate->window() != nullptr && candidate->width() > 0 && candidate->height() > 0;
            });
            found = shown == items.end() ? nullptr : *shown;
            return found != nullptr;
        },
        timeout);
    return found == nullptr ? std::nullopt : std::optional<Json>{node(found)};
}

}  // namespace emerick::app
