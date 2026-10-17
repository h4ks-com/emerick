#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>

namespace emerick::app {

// Draws a MIDI clip's notes as bars, spread over the clip's key range. It repaints only when a property changes.
class ClipNotes : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList notes READ notes WRITE setNotes NOTIFY notesChanged)
    Q_PROPERTY(qint64 length READ length WRITE setLength NOTIFY notesChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY notesChanged)

public:
    explicit ClipNotes(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList notes() const { return notes_; }
    [[nodiscard]] qint64 length() const { return length_; }
    [[nodiscard]] QColor color() const { return color_; }
    void setNotes(const QVariantList& value);
    void setLength(qint64 value);
    void setColor(const QColor& value);

    void paint(QPainter* painter) override;

signals:
    void notesChanged();

private:
    QVariantList notes_;
    qint64 length_ = 0;
    QColor color_;
};

}  // namespace emerick::app
