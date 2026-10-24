#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

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

    // What the pickers offer, as {id, name} entries: roots and bass notes are note names, qualities are chord type
    // suffixes, and sections are the names yamaha.section.set takes.
    Q_INVOKABLE static QVariantList chordRoots();
    Q_INVOKABLE static QVariantList chordBasses();
    Q_INVOKABLE static QVariantList chordQualities();
    Q_INVOKABLE static QVariantList sectionChoices();
    // The root, quality and bass ids of a chord name, empty where the name has none.
    Q_INVOKABLE static QVariantMap chordParts(const QString& name);
};

}  // namespace emerick::yamaha
