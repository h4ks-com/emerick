#include "emerick/app/spectrogram.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <span>
#include <utility>

namespace emerick::app {

namespace {

constexpr const char* kTapOwner = "spectrogram";
constexpr const char* kCategory = "Settings / Tuner and spectrogram";
constexpr const char* kLocation = "settings.monitors";
constexpr double kMinRowsPerSecond = 60.0;
constexpr double kMinApertureDb = 10.0;
constexpr double kPeakFloorDb = -120.0;
constexpr double kStateDecimals = 10.0;
constexpr std::array kFftSizes{"1024", "2048", "4096", "8192", "16384", "32768"};
constexpr const char* kDefaultFftSize = "4096";
constexpr const char* kLogAxis = "Logarithmic";
constexpr std::array kAxes{kLogAxis, "Linear"};

struct NamedWindow {
    engine::SpectrumWindow window;
    const char* id;
};

constexpr std::array kWindows{NamedWindow{engine::SpectrumWindow::blackmanHarris, "Blackman-Harris"},
                              NamedWindow{engine::SpectrumWindow::blackman, "Blackman"},
                              NamedWindow{engine::SpectrumWindow::hann, "Hann"}};

double rounded(double value) { return std::round(value * kStateDecimals) / kStateDecimals; }

Json choiceIds(std::span<const char* const> ids) {
    return ids | std::views::transform([](const char* id) { return Json(id); }) | std::ranges::to<Json::array_t>();
}

void addSetting(Registry& registry, Setting setting) {
    setting.category = kCategory;
    setting.location = kLocation;
    registry.addSetting(std::move(setting));
}

}  // namespace

void registerSpectrogramSettings(Registry& registry) {
    addSetting(registry, {.id = kSpectrogramFftSetting,
                          .title = "Spectrogram FFT size",
                          .aliases = {"frequency resolution", "fft length"},
                          .type = SettingType::Choice,
                          .defaultValue = kDefaultFftSize,
                          .choices = choiceIds(kFftSizes),
                          .hint = "A larger FFT tells closer frequencies apart and blurs fast changes"});
    addSetting(registry,
               {.id = kSpectrogramWindowSetting,
                .title = "Spectrogram window",
                .aliases = {"fft window", "window function"},
                .type = SettingType::Choice,
                .defaultValue = kWindows.front().id,
                .choices = kWindows | std::views::transform([](const NamedWindow& entry) { return Json(entry.id); }) |
                           std::ranges::to<Json::array_t>(),
                .hint = "Blackman-Harris leaks least between bins, Hann has the narrowest peaks"});
    addSetting(registry, {.id = kSpectrogramCeilingSetting,
                          .title = "Spectrogram ceiling",
                          .aliases = {"upper db", "brightest level"},
                          .type = SettingType::Number,
                          .defaultValue = 0,
                          .minimum = -60,
                          .maximum = 0,
                          .hint = "The level in dBFS that draws brightest"});
    addSetting(registry, {.id = kSpectrogramFloorSetting,
                          .title = "Spectrogram floor",
                          .aliases = {"lower db", "noise floor"},
                          .type = SettingType::Number,
                          .defaultValue = -100,
                          .minimum = -160,
                          .maximum = -10,
                          .hint = "The level in dBFS that draws black"});
    addSetting(registry, {.id = kSpectrogramAxisSetting,
                          .title = "Spectrogram frequency axis",
                          .aliases = {"log frequency", "linear frequency"},
                          .type = SettingType::Choice,
                          .defaultValue = kLogAxis,
                          .choices = choiceIds(kAxes),
                          .hint = "Logarithmic gives every octave the same width"});
    addSetting(registry, {.id = kSpectrogramSpectrumSetting,
                          .title = "Show spectrum line",
                          .aliases = {"spectrum analyzer", "peak hold"},
                          .type = SettingType::Boolean,
                          .defaultValue = true,
                          .hint = "Draws the newest spectrum and its decaying peaks above the waterfall"});
}

SpectrogramSettings spectrogramSettings(const Registry& registry) {
    const auto text = [&](const char* id) { return registry.settingValue(id).get<std::string>(); };
    const auto* const window = std::ranges::find(kWindows, text(kSpectrogramWindowSetting),
                                                 [](const NamedWindow& entry) { return std::string{entry.id}; });
    const double ceiling = registry.settingValue(kSpectrogramCeilingSetting).get<double>();
    return {
        .fftSize = std::stoul(text(kSpectrogramFftSetting)),
        .window = window == kWindows.end() ? kWindows.front().window : window->window,
        .ceilingDb = ceiling,
        .floorDb = std::min(registry.settingValue(kSpectrogramFloorSetting).get<double>(), ceiling - kMinApertureDb),
        .logAxis = text(kSpectrogramAxisSetting) == kLogAxis,
        .showSpectrum = registry.settingValue(kSpectrogramSpectrumSetting).get<bool>()};
}

Spectrogram::Spectrogram(Registry& registry, ProjectController& controller, Nav& nav, QObject* parent)
    : QObject(parent),
      registry_(registry),
      controller_(controller),
      nav_(nav),
      settings_(spectrogramSettings(registry)) {
    connect(&nav_, &Nav::changed, this, &Spectrogram::follow);
    connect(&controller_, &ProjectController::projectChanged, this, [this] {
        follow();
        emit sourcesChanged();
    });
    connect(&registry_, &Registry::settingsChanged, this, &Spectrogram::readSettings);
}

QVariantList Spectrogram::sources() const {
    return sourceChoices(controller_.project().document(), QStringLiteral("Master"));
}

double Spectrogram::binHz() const { return sampleRate() / static_cast<double>(settings_.fftSize); }

double Spectrogram::rowsPerSecond() const { return hop_ > 0 ? sampleRate() / static_cast<double>(hop_) : 0.0; }

void Spectrogram::setSource(const std::string& id) {
    checkSourceId(controller_.project().document(), id);
    chosen_ = id;
    emit sourceChanged();
    follow();
}

void Spectrogram::readSettings() {
    const SpectrogramSettings next = spectrogramSettings(registry_);
    if (next == settings_) {
        return;
    }
    const bool analysisChanged = next.fftSize != settings_.fftSize || next.window != settings_.window;
    settings_ = next;
    if (analysisChanged && reader_) {
        source_.reset();
        follow();
    }
    emit settingsChanged();
}

void Spectrogram::follow() {
    const std::optional<MonitorSource> wanted =
        nav_.spectrogramOpen() ? resolveSource(controller_.project().document(), chosen_, {}, {.masterByDefault = true})
                               : std::nullopt;
    if (wanted == source_ && (reader_ != nullptr) == wanted.has_value()) {
        return;
    }
    source_ = wanted;
    reader_.reset();
    {
        const std::scoped_lock lock(rowsMutex_);
        rows_.clear();
        peaks_.clear();
    }
    if (!wanted) {
        controller_.setTap(kTapOwner, nullptr);
    } else {
        auto tap = std::make_shared<engine::SignalTap>(wanted->trackId, wanted->point);
        engine::SpectrogramAnalyzer analyzer(settings_.fftSize, settings_.window, sampleRate(), kMinRowsPerSecond);
        hop_ = analyzer.hop();
        reader_ = std::make_unique<TapReader>(tap, [this, analyzer](std::span<const float> samples) mutable {
            std::vector<std::vector<float>> rows = analyzer.add(samples);
            if (rows.empty()) {
                return;
            }
            std::vector<engine::SpectrumPeak> peaks =
                engine::findPeaks(rows.back(), analyzer.binHz(), kPeaks, kPeakFloorDb);
            const std::scoped_lock lock(rowsMutex_);
            std::ranges::move(rows, std::back_inserter(rows_));
            while (rows_.size() > kQueuedRows) {
                rows_.pop_front();
            }
            peaks_ = std::move(peaks);
        });
        controller_.setTap(kTapOwner, std::move(tap));
    }
    emit sourceChanged();
    emit restarted();
}

std::vector<std::vector<float>> Spectrogram::takeRows() {
    const std::scoped_lock lock(rowsMutex_);
    std::vector<std::vector<float>> rows(std::make_move_iterator(rows_.begin()), std::make_move_iterator(rows_.end()));
    rows_.clear();
    return rows;
}

std::vector<engine::SpectrumPeak> Spectrogram::peaks() const {
    const std::scoped_lock lock(rowsMutex_);
    return peaks_;
}

Json Spectrogram::state() const {
    return {{"open", nav_.spectrogramOpen()},
            {"source", sourceJson(source_)},
            {"chosenSource", chosen_},
            {"fftSize", settings_.fftSize},
            {"window", std::ranges::find(kWindows, settings_.window, &NamedWindow::window)->id},
            {"binHz", rounded(binHz())},
            {"rowsPerSecond", rounded(rowsPerSecond())},
            {"ceilingDb", settings_.ceilingDb},
            {"floorDb", settings_.floorDb},
            {"frequencyAxis", settings_.logAxis ? "log" : "linear"},
            {"peaks", peaks() | std::views::transform([](const engine::SpectrumPeak& peak) {
                          return Json{{"hz", rounded(peak.hz)}, {"db", rounded(peak.db)}};
                      }) | std::ranges::to<Json::array_t>()}};
}

}  // namespace emerick::app
