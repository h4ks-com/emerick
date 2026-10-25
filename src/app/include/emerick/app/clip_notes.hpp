#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <vector>

namespace emerick::app {

// Draws a MIDI clip's notes as bars, spread over the clip's key range or over lowKey to highKey when both are set. It
// repaints only when a property changes.
class ClipNotes : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList notes READ notes WRITE setNotes NOTIFY notesChanged)
    Q_PROPERTY(qint64 length READ length WRITE setLength NOTIFY notesChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY notesChanged)
    Q_PROPERTY(int lowKey READ lowKey WRITE setLowKey NOTIFY notesChanged)
    Q_PROPERTY(int highKey READ highKey WRITE setHighKey NOTIFY notesChanged)

public:
    explicit ClipNotes(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList notes() const { return notes_; }
    [[nodiscard]] qint64 length() const { return length_; }
    [[nodiscard]] QColor color() const { return color_; }
    [[nodiscard]] int lowKey() const { return lowKey_; }
    [[nodiscard]] int highKey() const { return highKey_; }
    void setNotes(const QVariantList& value);
    void setLength(qint64 value);
    void setColor(const QColor& value);
    void setLowKey(int value);
    void setHighKey(int value);

    void paint(QPainter* painter) override;

signals:
    void notesChanged();

private:
    struct Bar {
        double start = 0;
        double length = 0;
        int pitch = 0;
    };

    void setKey(int& key, int value);

    QVariantList notes_;
    std::vector<Bar> bars_;
    qint64 length_ = 0;
    QColor color_;
    int lowKey_ = -1;
    int highKey_ = -1;
};

}  // namespace emerick::app
