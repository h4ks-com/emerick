#pragma once

#include <QTemporaryDir>
#include <QTimer>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/registry.hpp"
#include "qt_app.hpp"

namespace emerick::app::test {

inline constexpr double kRate = 48000.0;
inline constexpr core::Frames kBlock = 64;
inline const std::string kMidiPort = "Keyboard";

// A controller on the offline audio backend and a virtual MIDI port, with every project action registered.
struct ProjectRig {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    engine::OfflineBackend* audio = nullptr;
    engine::VirtualMidiBackend* midi = nullptr;
    std::unique_ptr<ProjectController> controller;
    // The offline backend only runs when asked, so while the controller waits on the engine this plays the device.
    QTimer clock;

    ProjectRig() {
        ensureApplication();
        auto audioOwner = std::make_unique<engine::OfflineBackend>(
            std::vector<engine::AudioDeviceInfo>{{"interface", "Interface", 2, 2}});
        auto midiOwner = std::make_unique<engine::VirtualMidiBackend>(std::vector{kMidiPort}, std::vector{kMidiPort});
        audio = audioOwner.get();
        midi = midiOwner.get();
        controller = std::make_unique<ProjectController>(registry, std::move(audioOwner), std::move(midiOwner),
                                                         engine::Engine::Options{kRate, kBlock});
        registerProjectActions(registry, *controller, nav);
        QObject::connect(&clock, &QTimer::timeout, [this] { audio->process(kBlock); });
        clock.start(1);
    }

    Json run(const std::string& id, const Json& params = Json::object()) const { return registry.run(id, params); }

    [[nodiscard]] std::filesystem::path path(const std::string& name) const {
        return std::filesystem::path{dir.path().toStdString()} / name;
    }

    // Waits for the writer thread to finish takes, processing blocks so the engine keeps running.
    int waitForTakes(int count) const {
        int collected = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (collected < count && std::chrono::steady_clock::now() < deadline) {
            audio->process(kBlock);
            collected += controller->collectTakes();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return collected;
    }
};

inline void writeConstant(const std::filesystem::path& path, core::Frames frames, float value) {
    const std::vector<float> samples(static_cast<std::size_t>(frames), value);
    writeWav(path, {samples}, kRate);
}

}  // namespace emerick::app::test
