#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QObject>
#include <QVariant>
#include <QVariantList>

namespace emerick::yamaha {

// What the lanes draw: one block per event with its tick range, label and color, read from a track's data.
class LaneBlocks : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE static QVariantList chordBlocks(const QVariant& data);
    Q_INVOKABLE static QVariantList sectionBlocks(const QVariant& data);
};

}  // namespace emerick::yamaha
