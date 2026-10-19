#pragma once

#include <QEvent>
#include <QObject>

namespace emerick::app {

// Keeps single keys such as Space for the window's shortcuts. A press outside the focused text field ends its editing,
// and a focused control that is no text field never takes a plain key from a shortcut.
class KeyFocusFilter final : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override;
};

}  // namespace emerick::app
