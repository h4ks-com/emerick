#include "emerick/app/key_focus.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace emerick::app {

namespace {

bool typesText(QObject* focus) {
    if (focus == nullptr) {
        return false;
    }
    QInputMethodQueryEvent query(Qt::ImEnabled | Qt::ImReadOnly);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool() && !query.value(Qt::ImReadOnly).toBool();
}

bool isPlainKey(const QKeyEvent& key) {
    const Qt::KeyboardModifiers commandModifiers = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    return (key.modifiers() & commandModifiers) == Qt::NoModifier && !key.text().isEmpty() &&
           key.text().front().isPrint();
}

// A picker's list is a popup in the window's overlay, and the picker moves its own field's focus.
bool insidePopup(const QQuickWindow& window, QPointF scenePosition) {
    const auto* overlay = window.property("overlay").value<QQuickItem*>();
    if (overlay == nullptr) {
        return false;
    }
    const QPointF at = overlay->mapFromScene(scenePosition);
    return overlay->childAt(at.x(), at.y()) != nullptr;
}

void endTextEditing(QQuickWindow& window, QPointF scenePosition) {
    QQuickItem* focus = window.activeFocusItem();
    if (focus == nullptr || !typesText(focus) || focus->contains(focus->mapFromScene(scenePosition)) ||
        insidePopup(window, scenePosition)) {
        return;
    }
    focus->setFocus(false);
}

}  // namespace

bool KeyFocusFilter::eventFilter(QObject* watched, QEvent* event) {
    if (const auto* key = dynamic_cast<QKeyEvent*>(event); key != nullptr && key->type() == QEvent::ShortcutOverride &&
                                                           isPlainKey(*key) &&
                                                           !typesText(QGuiApplication::focusObject())) {
        event->ignore();
        return true;
    }
    if (const auto* press = dynamic_cast<QMouseEvent*>(event);
        press != nullptr && press->type() == QEvent::MouseButtonPress) {
        if (auto* window = qobject_cast<QQuickWindow*>(watched)) {
            endTextEditing(*window, press->scenePosition());
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace emerick::app
