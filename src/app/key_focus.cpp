#include "emerick/app/key_focus.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>
#include <algorithm>
#include <array>
#include <span>

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

constexpr std::array kEditingKeys{
    QKeySequence::Copy,
    QKeySequence::Cut,
    QKeySequence::Paste,
    QKeySequence::SelectAll,
    QKeySequence::Undo,
    QKeySequence::Redo,
    QKeySequence::MoveToStartOfLine,
    QKeySequence::MoveToEndOfLine,
    QKeySequence::MoveToStartOfDocument,
    QKeySequence::MoveToEndOfDocument,
    QKeySequence::MoveToPreviousWord,
    QKeySequence::MoveToNextWord,
    QKeySequence::SelectStartOfLine,
    QKeySequence::SelectEndOfLine,
    QKeySequence::SelectStartOfDocument,
    QKeySequence::SelectEndOfDocument,
    QKeySequence::SelectPreviousWord,
    QKeySequence::SelectNextWord,
    QKeySequence::DeleteStartOfWord,
    QKeySequence::DeleteEndOfWord,
};

bool matchesAny(const QKeyEvent& key, std::span<const QKeySequence::StandardKey> keys) {
    return std::ranges::any_of(keys, [&key](QKeySequence::StandardKey standard) { return key.matches(standard); });
}

// A read-only text view such as the console log copies its selection and selects all of its text.
bool editsText(QObject* focus, const QKeyEvent& key) {
    if (focus == nullptr) {
        return false;
    }
    if (typesText(focus)) {
        return matchesAny(key, kEditingKeys);
    }
    const QVariant selection = focus->property("selectedText");
    return selection.isValid() && (key.matches(QKeySequence::SelectAll) ||
                                   (key.matches(QKeySequence::Copy) && !selection.toString().isEmpty()));
}

// A picker's list is a popup in the window's overlay, and the picker moves its own field's focus.
bool insidePopup(const QQuickWindow& window, QPointF scenePosition) {
    for (const QQuickItem* layer : window.contentItem()->childItems()) {
        if (layer->inherits("QQuickOverlay")) {
            const QPointF at = layer->mapFromScene(scenePosition);
            return layer->childAt(at.x(), at.y()) != nullptr;
        }
    }
    return false;
}

// A control such as a number field whose step arrows sit beside its editor marks itself with editScope, so a press
// anywhere on it keeps the editor open.
QQuickItem* editArea(QQuickItem* focus) {
    for (QQuickItem* item = focus; item != nullptr; item = item->parentItem()) {
        if (item->property("editScope").toBool()) {
            return item;
        }
    }
    return focus;
}

void endTextEditing(QQuickWindow& window, QPointF scenePosition) {
    QQuickItem* focus = window.activeFocusItem();
    if (focus == nullptr || !typesText(focus)) {
        return;
    }
    QQuickItem* area = editArea(focus);
    if (area->contains(area->mapFromScene(scenePosition)) || insidePopup(window, scenePosition)) {
        return;
    }
    focus->setFocus(false);
}

}  // namespace

bool KeyFocusFilter::eventFilter(QObject* watched, QEvent* event) {
    if (const auto* key = dynamic_cast<QKeyEvent*>(event); key != nullptr && key->type() == QEvent::ShortcutOverride) {
        QObject* focus = QGuiApplication::focusObject();
        if (editsText(focus, *key)) {
            event->accept();
            return true;
        }
        if (isPlainKey(*key) && !typesText(focus)) {
            event->ignore();
            return true;
        }
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
