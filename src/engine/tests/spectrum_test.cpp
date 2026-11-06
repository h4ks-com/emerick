#include "emerick/engine/spectrum.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

using Catch::Approx;

TEST_CASE("the spectrum of a sine peaks at its bin at its level") {
    constexpr std::size_t kSize = 1024;
    constexpr std::size_t kBin = 64;
    std::vector<float> frame(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        frame[i] = static_cast<float>(
            0.5 * std::sin(2 * std::numbers::pi * kBin * static_cast<double>(i) / static_cast<double>(kSize)));
    }
    const std::vector<double> db = emerick::engine::spectrumDb(frame);
    REQUIRE(db.size() == (kSize / 2) + 1);
    CHECK(static_cast<std::size_t>(std::ranges::max_element(db) - db.begin()) == kBin);
    CHECK(db[kBin] == Approx(20 * std::log10(0.5)).margin(0.01));
    CHECK(db[kBin + 10] < -100);
}

TEST_CASE("the power spectrum of a frame matches its Hann windowed DFT bin by bin") {
    constexpr std::size_t kSize = 256;
    std::vector<float> frame(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        frame[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)) +
                                      (0.2 * std::cos(1.9 * static_cast<double>(i * i % 97))));
    }
    const std::vector<double> power = emerick::engine::powerSpectrum(frame);
    REQUIRE(power.size() == (kSize / 2) + 1);
    for (std::size_t bin = 0; bin < power.size(); ++bin) {
        std::complex<double> sum = 0;
        for (std::size_t i = 0; i < kSize; ++i) {
            const double angle = 2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(kSize);
            sum += (0.5 - (0.5 * std::cos(angle))) * frame[i] * std::polar(1.0, -angle * static_cast<double>(bin));
        }
        CHECK(power[bin] == Approx(std::norm(sum)).epsilon(1e-9).margin(1e-9));
    }
}

TEST_CASE("the spectrogram reads a full scale sine at 0 dB on its bin, for every window") {
    using namespace emerick::engine;
    constexpr double kRate = 48000.0;
    constexpr std::size_t kSize = 4096;
    const SpectrumWindow window =
        GENERATE(SpectrumWindow::blackmanHarris, SpectrumWindow::blackman, SpectrumWindow::hann);
    SpectrogramAnalyzer analyzer(kSize, window, kRate, 60.0);
    const double binHz = analyzer.binHz();
    const double hz = 100 * binHz;
    std::vector<float> samples(kSize * 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    }
    const std::vector<std::vector<float>> rows = analyzer.add(samples);
    CHECK(rows.size() == 1 + ((kSize * 2) - kSize) / analyzer.hop());
    CHECK(analyzer.hop() <= static_cast<std::size_t>(kRate / 60.0));
    const std::vector<float>& row = rows.back();
    REQUIRE(row.size() == kSize / 2);
    CHECK(row[100] == Approx(0.0).margin(0.01));
    CHECK(row[140] < -90.0F);
}

TEST_CASE("the peak finder lists the loudest peaks between their bins, loudest first") {
    using namespace emerick::engine;
    std::vector<float> db(512, -120.0F);
    const auto bump = [&](std::size_t bin, float top, float left, float right) {
        db[bin - 1] = left;
        db[bin] = top;
        db[bin + 1] = right;
    };
    bump(40, -6.0F, -12.0F, -12.0F);
    bump(100, -20.0F, -21.0F, -30.0F);
    bump(300, -40.0F, -50.0F, -50.0F);
    db[42] = -11.0F;
    const std::vector<SpectrumPeak> peaks = findPeaks(db, 10.0, 2, -100.0);
    REQUIRE(peaks.size() == 2);
    CHECK(peaks[0].hz == Approx(400.0));
    CHECK(peaks[0].db == Approx(-6.0));
    CHECK(peaks[1].hz > 995.0);
    CHECK(peaks[1].hz < 1000.0);
    CHECK(peaks[1].db > -20.0);
    CHECK(findPeaks(db, 10.0, 10, -30.0).size() == 2);
}
