#include "emerick/app/spectrogram_view.hpp"

#include <rhi/qrhi.h>

#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

#include "emerick/engine/pitch.hpp"

namespace emerick::app {

namespace {

constexpr float kSilentDb = -200.0F;
constexpr double kLowestHz = 20.0;
constexpr int kMaxColumns = 4096;
constexpr int kMaxRows = 2048;
constexpr int kPullMs = 16;
constexpr double kPeakDecayDbPerSecond = 20.0;
constexpr double kReadoutReferenceHz = 440.0;
constexpr double kLevelTickDb = 20.0;
constexpr double kMinTickGapPixels = 60.0;
constexpr std::array kLogTicks{20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};
constexpr std::array kLinearSteps{500.0, 1000.0, 2000.0, 5000.0, 10000.0};
constexpr int kPaletteSize = 256;

struct PaletteStop {
    double at;
    std::array<int, 3> rgb;
};

constexpr std::array kPaletteStops{PaletteStop{0.0, {0, 0, 0}},       PaletteStop{0.18, {16, 12, 70}},
                                   PaletteStop{0.38, {96, 24, 140}},  PaletteStop{0.58, {206, 62, 76}},
                                   PaletteStop{0.74, {245, 130, 30}}, PaletteStop{0.9, {252, 222, 74}},
                                   PaletteStop{1.0, {255, 255, 255}}};

// Runs from black through blue, purple and orange to yellow and white, so louder always reads brighter.
const std::array<QRgb, kPaletteSize>& palette() {
    static const std::array<QRgb, kPaletteSize> colors = [] {
        std::array<QRgb, kPaletteSize> built{};
        for (int index = 0; index < kPaletteSize; ++index) {
            const double at = index / static_cast<double>(kPaletteSize - 1);
            const auto* upper =
                std::ranges::find_if(kPaletteStops, [at](const PaletteStop& stop) { return stop.at >= at; });
            const PaletteStop& high = *upper;
            const PaletteStop& low = upper == kPaletteStops.begin() ? high : *std::prev(upper);
            const double mix = high.at > low.at ? (at - low.at) / (high.at - low.at) : 0.0;
            const auto channel = [&](std::size_t which) {
                return static_cast<int>(
                    std::lround(low.rgb.at(which) + ((high.rgb.at(which) - low.rgb.at(which)) * mix)));
            };
            built.at(static_cast<std::size_t>(index)) = qRgb(channel(0), channel(1), channel(2));
        }
        return built;
    }();
    return colors;
}

QString frequencyLabel(double hz) {
    constexpr double kKilo = 1000.0;
    return hz >= kKilo ? QString::number(hz / kKilo) + "k" : QString::number(hz);
}

struct RhiTextureDeleter {
    void operator()(QRhiTexture* texture) const { texture->deleteLater(); }
};

// Keeps the ring image on the GPU: a new image uploads whole, and later rows upload one by one into their place.
class RingTexture final : public QSGTexture {
public:
    void setImage(const QImage& image) {
        image_ = image;
        uploadAll_ = true;
        rows_.clear();
    }
    void addRow(int row, QImage pixels) { rows_.emplace_back(row, std::move(pixels)); }

    [[nodiscard]] qint64 comparisonKey() const override { return key_; }
    [[nodiscard]] QRhiTexture* rhiTexture() const override { return texture_.get(); }
    [[nodiscard]] QSize textureSize() const override { return image_.size(); }
    [[nodiscard]] bool hasAlphaChannel() const override { return false; }
    [[nodiscard]] bool hasMipmaps() const override { return false; }

    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* updates) override {
        if (!texture_ || texture_->pixelSize() != image_.size()) {
            texture_.reset(rhi->newTexture(QRhiTexture::RGBA8, image_.size()));
            texture_->create();
            uploadAll_ = true;
        }
        if (std::exchange(uploadAll_, false)) {
            updates->uploadTexture(texture_.get(), image_);
            rows_.clear();
        }
        for (auto& [row, pixels] : rows_) {
            QRhiTextureSubresourceUploadDescription description(pixels);
            description.setDestinationTopLeft(QPoint(0, row));
            updates->uploadTexture(texture_.get(), QRhiTextureUploadDescription({0, 0, description}));
        }
        rows_.clear();
    }

private:
    static inline std::atomic<qint64> nextKey_{1};
    qint64 key_ = nextKey_++;
    QImage image_;
    bool uploadAll_ = true;
    std::vector<std::pair<int, QImage>> rows_;
    std::unique_ptr<QRhiTexture, RhiTextureDeleter> texture_;
};

// The scene graph owns these nodes as children of the root, and the root owns the texture they share.
struct WaterfallNode : QSGNode {
    std::unique_ptr<QSGTexture> texture;
    QSGRectangleNode* band = nullptr;
    QSGImageNode* newer = nullptr;
    QSGImageNode* older = nullptr;
    QSGGeometryNode* spectrum = nullptr;
    QSGGeometryNode* peaks = nullptr;
};

QSGGeometryNode* lineNode() {
    auto node = std::make_unique<QSGGeometryNode>();
    auto geometry = std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
    node->setGeometry(geometry.release());
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(std::make_unique<QSGFlatColorMaterial>().release());
    node->setFlag(QSGNode::OwnsMaterial);
    return node.release();
}

}  // namespace

SpectrogramView::SpectrogramView(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    timer_.setInterval(kPullMs);
    connect(&timer_, &QTimer::timeout, this, &SpectrogramView::pull);
}

void SpectrogramView::setMonitor(Spectrogram* value) {
    if (monitor_ == value) {
        return;
    }
    if (monitor_) {
        disconnect(monitor_, nullptr, this, nullptr);
    }
    monitor_ = value;
    if (monitor_) {
        connect(monitor_, &Spectrogram::restarted, this, &SpectrogramView::restart);
        connect(monitor_, &Spectrogram::settingsChanged, this, [this] {
            const SpectrogramSettings& next = monitor_->settings();
            if (next.logAxis != drawn_.logAxis || next.showSpectrum != drawn_.showSpectrum ||
                next.fftSize != drawn_.fftSize) {
                restart();
            } else {
                drawn_ = next;
                recolor();
                emit layoutChanged();
            }
        });
        timer_.start();
    } else {
        timer_.stop();
    }
    restart();
    emit monitorChanged();
}

void SpectrogramView::setSpectrumHeight(double value) {
    if (spectrumHeight_ != value) {
        spectrumHeight_ = value;
        restart();
    }
}

void SpectrogramView::setLineColor(const QColor& value) {
    lineColor_ = value;
    emit colorsChanged();
    update();
}

void SpectrogramView::setPeakColor(const QColor& value) {
    peakColor_ = value;
    emit colorsChanged();
    update();
}

void SpectrogramView::setBandColor(const QColor& value) {
    bandColor_ = value;
    emit colorsChanged();
    update();
}

double SpectrogramView::waterfallTop() const { return drawn_.showSpectrum ? spectrumHeight_ : 0.0; }

double SpectrogramView::highestHz() const { return monitor_ ? monitor_->sampleRate() / 2 : kLowestHz * 2; }

double SpectrogramView::frequencyAt(double fraction) const {
    const double highest = highestHz();
    return drawn_.logAxis ? kLowestHz * std::pow(highest / kLowestHz, fraction) : highest * fraction;
}

double SpectrogramView::fractionOf(double hz) const {
    const double highest = highestHz();
    return drawn_.logAxis ? std::log(hz / kLowestHz) / std::log(highest / kLowestHz) : hz / highest;
}

double SpectrogramView::rowHeight() const { return rows_ > 0 ? (height() - waterfallTop()) / rows_ : 0.0; }

QRgb SpectrogramView::color(float db) const {
    const double share = (db - drawn_.floorDb) / (drawn_.ceilingDb - drawn_.floorDb);
    return palette().at(static_cast<std::size_t>(std::clamp(share, 0.0, 1.0) * (kPaletteSize - 1)));
}

double SpectrogramView::spectrumY(float db) const {
    const double share = std::clamp((db - drawn_.floorDb) / (drawn_.ceilingDb - drawn_.floorDb), 0.0, 1.0);
    return 1.0 + ((1.0 - share) * (spectrumHeight_ - 2.0));
}

void SpectrogramView::restart() {
    drawn_ = monitor_ ? monitor_->settings() : SpectrogramSettings{};
    const double ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const double waterfallHeight = height() - waterfallTop();
    const bool drawable = monitor_ && drawn_.fftSize > 0 && width() >= 1 && waterfallHeight >= 1;
    columns_ = drawable ? std::clamp(static_cast<int>(std::ceil(width() * ratio)), 1, kMaxColumns) : 0;
    rows_ = drawable ? std::clamp(static_cast<int>(std::ceil(waterfallHeight * ratio)), 1, kMaxRows) : 0;
    const auto columns = static_cast<std::size_t>(columns_);
    const std::size_t bins = drawn_.fftSize / 2;
    const double binHz = monitor_ ? monitor_->binHz() : 1.0;
    columnBins_.assign(columns, {});
    for (std::size_t column = 0; column < columns; ++column) {
        const double from = frequencyAt(static_cast<double>(column) / columns_) / binHz;
        const double to = frequencyAt(static_cast<double>(column + 1) / columns_) / binHz;
        ColumnBins& entry = columnBins_[column];
        if (to - from >= 1.0) {
            entry.first = std::min(static_cast<std::size_t>(from), bins - 1);
            entry.last = std::clamp(static_cast<std::size_t>(std::ceil(to)), entry.first + 1, bins);
        } else {
            const double center = std::clamp((from + to) / 2, 0.0, static_cast<double>(bins) - 2);
            entry.first = static_cast<std::size_t>(center);
            entry.fraction = center - static_cast<double>(entry.first);
        }
    }
    levels_.assign(columns * static_cast<std::size_t>(rows_), kSilentDb);
    image_ = QImage(std::max(columns_, 1), std::max(rows_, 1), QImage::Format_RGBA8888);
    image_.fill(color(kSilentDb));
    spectrum_.assign(columns, kSilentDb);
    peakHold_.assign(columns, kSilentDb);
    head_ = 0;
    filled_ = 0;
    rowsDrawn_ = 0;
    dirtyRows_.clear();
    fullUpload_ = true;
    emit layoutChanged();
    emit rowsDrawnChanged();
    update();
}

void SpectrogramView::recolor() {
    for (int row = 0; row < rows_; ++row) {
        for (int column = 0; column < columns_; ++column) {
            image_.setPixel(column, row,
                            color(levels_[(static_cast<std::size_t>(row) * static_cast<std::size_t>(columns_)) +
                                          static_cast<std::size_t>(column)]));
        }
    }
    fullUpload_ = true;
    update();
}

void SpectrogramView::addRow(const std::vector<float>& row) {
    if (row.size() < 2) {
        return;
    }
    head_ = (head_ + static_cast<std::size_t>(rows_) - 1) % static_cast<std::size_t>(rows_);
    const std::span levels =
        std::span(levels_).subspan(head_ * static_cast<std::size_t>(columns_), static_cast<std::size_t>(columns_));
    for (std::size_t column = 0; column < levels.size(); ++column) {
        const ColumnBins& bins = columnBins_[column];
        const float level =
            bins.last > bins.first
                ? *std::max_element(row.begin() + static_cast<std::ptrdiff_t>(bins.first),
                                    row.begin() + static_cast<std::ptrdiff_t>(std::min(bins.last, row.size())))
                : static_cast<float>(row[bins.first] + ((row[bins.first + 1] - row[bins.first]) * bins.fraction));
        levels[column] = level;
        image_.setPixel(static_cast<int>(column), static_cast<int>(head_), color(level));
        spectrum_[column] = level;
        peakHold_[column] = std::max(peakHold_[column], level);
    }
    filled_ = std::min(filled_ + 1, static_cast<std::size_t>(rows_));
    ++rowsDrawn_;
    if (dirtyRows_.size() >= static_cast<std::size_t>(rows_)) {
        fullUpload_ = true;
    } else {
        dirtyRows_.push_back(static_cast<int>(head_));
    }
}

void SpectrogramView::pull() {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - std::exchange(lastPull_, now)).count();
    if (!monitor_ || columns_ == 0 || !isVisible()) {
        return;
    }
    const std::vector<std::vector<float>> rows = monitor_->takeRows();
    const auto decay = static_cast<float>(std::min(seconds, 1.0) * kPeakDecayDbPerSecond);
    for (std::size_t column = 0; column < peakHold_.size(); ++column) {
        peakHold_[column] = std::max(peakHold_[column] - decay, spectrum_[column]);
    }
    for (const std::vector<float>& row : rows) {
        addRow(row);
    }
    if (!rows.empty()) {
        emit rowsDrawnChanged();
    }
    update();
}

QVariantList SpectrogramView::frequencyTicks() const {
    QVariantList ticks;
    if (columns_ == 0) {
        return ticks;
    }
    const auto add = [&](double hz) {
        const double x = fractionOf(hz) * width();
        if (x >= 0 && x <= width()) {
            ticks.append(QVariantMap{{"x", x}, {"label", frequencyLabel(hz)}});
        }
    };
    if (drawn_.logAxis) {
        std::ranges::for_each(kLogTicks, add);
        return ticks;
    }
    const double highest = highestHz();
    const double* step = std::ranges::find_if(
        kLinearSteps, [&](double candidate) { return candidate / highest * width() >= kMinTickGapPixels; });
    const double spacing = step == kLinearSteps.end() ? kLinearSteps.back() : *step;
    for (int index = 1; index * spacing < highest; ++index) {
        add(index * spacing);
    }
    return ticks;
}

QVariantList SpectrogramView::timeTicks() const {
    QVariantList ticks;
    const double perSecond = monitor_ ? monitor_->rowsPerSecond() * rowHeight() : 0.0;
    if (perSecond <= 0.0) {
        return ticks;
    }
    const int every = std::max(1, static_cast<int>(std::ceil(kMinTickGapPixels / perSecond)));
    for (int second = every; waterfallTop() + (second * perSecond) < height(); second += every) {
        ticks.append(
            QVariantMap{{"y", waterfallTop() + (second * perSecond)}, {"label", QString::number(second) + " s"}});
    }
    return ticks;
}

QVariantList SpectrogramView::levelTicks() const {
    QVariantList ticks;
    if (!drawn_.showSpectrum || columns_ == 0) {
        return ticks;
    }
    const double top = std::floor(drawn_.ceilingDb / kLevelTickDb) * kLevelTickDb;
    for (int index = 0; top - (index * kLevelTickDb) > drawn_.floorDb; ++index) {
        const double db = top - (index * kLevelTickDb);
        ticks.append(QVariantMap{{"y", spectrumY(static_cast<float>(db))}, {"label", QString::number(db)}});
    }
    return ticks;
}

QVariantMap SpectrogramView::readout(double x, double y) const {
    if (columns_ == 0 || width() <= 0) {
        return {};
    }
    const double fraction = std::clamp(x / width(), 0.0, 1.0);
    const double hz = frequencyAt(fraction);
    const std::size_t column = std::min(static_cast<std::size_t>(fraction * columns_), columnBins_.size() - 1);
    QVariantMap result{{"hz", hz},
                       {"note", qs(engine::noteName(engine::nearestNote(hz, kReadoutReferenceHz).midiNote))}};
    if (y < waterfallTop()) {
        result.insert("db", spectrum_[column]);
        return result;
    }
    const auto age = static_cast<std::size_t>((y - waterfallTop()) / rowHeight());
    if (age < filled_) {
        const std::size_t row = (head_ + age) % static_cast<std::size_t>(rows_);
        result.insert("db", levels_[(row * static_cast<std::size_t>(columns_)) + column]);
        result.insert("seconds", static_cast<double>(age) / monitor_->rowsPerSecond());
    }
    return result;
}

void SpectrogramView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        restart();
    }
}

QSGNode* SpectrogramView::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/) {
    if (columns_ == 0 || rows_ == 0) {
        const std::unique_ptr<QSGNode> discarded{old};
        return nullptr;
    }
    const bool software = window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    auto* root = dynamic_cast<WaterfallNode*>(old);
    if (root == nullptr) {
        root = std::make_unique<WaterfallNode>().release();
        root->band = window()->createRectangleNode();
        root->newer = window()->createImageNode();
        root->older = window()->createImageNode();
        for (QSGNode* child : std::initializer_list<QSGNode*>{root->band, root->newer, root->older}) {
            root->appendChildNode(child);
        }
        // The software renderer draws no custom geometry, so there the band shows without its lines.
        if (!software) {
            root->spectrum = lineNode();
            root->peaks = lineNode();
            root->appendChildNode(root->peaks);
            root->appendChildNode(root->spectrum);
        }
    }
    if (software) {
        if (fullUpload_ || !dirtyRows_.empty() || !root->texture) {
            root->texture.reset(window()->createTextureFromImage(image_));
        }
    } else {
        auto* ring = dynamic_cast<RingTexture*>(root->texture.get());
        if (ring == nullptr) {
            root->texture = std::make_unique<RingTexture>();
            ring = dynamic_cast<RingTexture*>(root->texture.get());
            fullUpload_ = true;
        }
        if (fullUpload_) {
            ring->setImage(image_);
        } else {
            for (const int row : dirtyRows_) {
                ring->addRow(row, image_.copy(0, row, columns_, 1));
            }
        }
    }
    fullUpload_ = false;
    dirtyRows_.clear();

    const double top = waterfallTop();
    const double step = rowHeight();
    const auto head = static_cast<int>(head_);
    for (QSGImageNode* node : {root->newer, root->older}) {
        node->setTexture(root->texture.get());
        node->setFiltering(QSGTexture::Linear);
        node->markDirty(QSGNode::DirtyMaterial);
    }
    root->newer->setSourceRect(QRectF(0, head, columns_, rows_ - head));
    root->newer->setRect(QRectF(0, top, width(), (rows_ - head) * step));
    root->older->setSourceRect(QRectF(0, 0, columns_, head));
    root->older->setRect(QRectF(0, top + ((rows_ - head) * step), width(), head * step));
    root->band->setColor(bandColor_);
    root->band->setRect(QRectF(0, 0, width(), top));

    const auto drawLine = [&](QSGGeometryNode* node, const std::vector<float>& levels, const QColor& color) {
        if (node == nullptr) {
            return;
        }
        auto* material = dynamic_cast<QSGFlatColorMaterial*>(node->material());
        if (material->color() != color) {
            material->setColor(color);
            node->markDirty(QSGNode::DirtyMaterial);
        }
        QSGGeometry* geometry = node->geometry();
        geometry->allocate(drawn_.showSpectrum ? columns_ : 0);
        const std::span vertices{geometry->vertexDataAsPoint2D(), static_cast<std::size_t>(geometry->vertexCount())};
        for (std::size_t column = 0; column < vertices.size(); ++column) {
            vertices[column].set(static_cast<float>((static_cast<double>(column) + 0.5) * width() / columns_),
                                 static_cast<float>(spectrumY(levels[column])));
        }
        node->markDirty(QSGNode::DirtyGeometry);
    };
    drawLine(root->peaks, peakHold_, peakColor_);
    drawLine(root->spectrum, spectrum_, lineColor_);
    return root;
}

}  // namespace emerick::app
