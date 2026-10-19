#pragma once

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
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
// CoreAudio names devices by UID, so the views must show the name instead.
inline const std::string kMotuId = "40-72-18-3D-42-0C:output";

// Counts each listing of the offline backend's devices, because in the app a listing asks CoreAudio about every device.
class CountingAudioBackend final : public engine::AudioBackend {
public:
    explicit CountingAudioBackend(std::unique_ptr<engine::OfflineBackend> offline) : offline_(std::move(offline)) {}

    std::vector<engine::AudioDeviceInfo> devices() override {
        ++listings;
        return offline_->devices();
    }
    engine::StreamLayout open(const engine::StreamRequest& request, engine::AudioCallback callback) override {
        ++opens;
        return offline_->open(request, std::move(callback));
    }
    void close() override { offline_->close(); }

    // Reports a device change from another thread, the way a hotplug notification arrives.
    void plug() {
        std::jthread([this] { deviceListChanged(); }).join();
    }

    int listings = 0;
    int opens = 0;

private:
    std::unique_ptr<engine::OfflineBackend> offline_;
};

// Counts each listing of the virtual backend's ports, because in the app a listing asks CoreMIDI about every port.
class CountingMidiBackend final : public engine::MidiBackend {
public:
    explicit CountingMidiBackend(std::unique_ptr<engine::VirtualMidiBackend> virtualMidi)
        : virtual_(std::move(virtualMidi)) {}

    std::vector<engine::MidiPortInfo> ports() override {
        ++listings;
        return virtual_->ports();
    }
    std::unique_ptr<engine::MidiInput> openInput(const std::string& portId, engine::MidiReceive receive) override {
        return virtual_->openInput(portId, std::move(receive));
    }
    std::unique_ptr<engine::MidiOutput> openOutput(const std::string& portId) override {
        return virtual_->openOutput(portId);
    }

    int listings = 0;

private:
    std::unique_ptr<engine::VirtualMidiBackend> virtual_;
};

// A controller on the offline audio backend and a virtual MIDI port, with every project action registered.
struct ProjectRig {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    engine::OfflineBackend* audio = nullptr;
    engine::VirtualMidiBackend* midi = nullptr;
    CountingAudioBackend* audioLister = nullptr;
    CountingMidiBackend* midiLister = nullptr;
    std::unique_ptr<ProjectController> controller;
    // The offline backend only runs when asked, so while the controller waits on the engine this plays the device.
    QTimer clock;

    // Each MIDI port name is both an input and an output.
    explicit ProjectRig(const std::vector<std::string>& midiPorts = {kMidiPort}) {
        ensureApplication();
        auto offline = std::make_unique<engine::OfflineBackend>(
            std::vector<engine::AudioDeviceInfo>{{"interface", "Interface", 2, 2},
                                                 {"keyboard", "Digital Keyboard", 2, 2, {44100.0}},
                                                 {kMotuId, "MOTU M2", 2, 4}});
        auto virtualMidi = std::make_unique<engine::VirtualMidiBackend>(midiPorts, midiPorts);
        audio = offline.get();
        midi = virtualMidi.get();
        auto audioOwner = std::make_unique<CountingAudioBackend>(std::move(offline));
        auto midiOwner = std::make_unique<CountingMidiBackend>(std::move(virtualMidi));
        audioLister = audioOwner.get();
        midiLister = midiOwner.get();
        controller = std::make_unique<ProjectController>(registry, std::move(audioOwner), std::move(midiOwner),
                                                         engine::Engine::Options{kRate, kBlock}, path("Projects"));
        registerProjectActions(registry, *controller, nav);
        QObject::connect(&clock, &QTimer::timeout, [this] { audio->process(kBlock); });
        clock.start(1);
    }

    Json run(const std::string& id, const Json& params = Json::object()) const { return registry.run(id, params); }

    // Runs the action the way an MCP tool call does: it reads every state provider, then the event loop turns once.
    Json runAsAgent(const std::string& id, const Json& params = Json::object()) const {
        Json result{{"result", registry.run(id, params)}, {"state", registry.allState()}};
        QCoreApplication::processEvents();
        return result;
    }

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

// Binds the route devices four times, as the inspector does, so a test pays for what the views read.
struct InspectorBindings {
    explicit InspectorBindings(ProjectController& controller) {
        qml.rootContext()->setContextProperty("Project", &controller);
        QQmlComponent component(&qml);
        component.setData(
            "import QtQml\n"
            "QtObject {\n"
            "    property var audioIn: Project.routeDevices.audioIn\n"
            "    property var audioOut: Project.routeDevices.audioOut\n"
            "    property var midiIn: Project.routeDevices.midiIn\n"
            "    property var midiOut: Project.routeDevices.midiOut\n"
            "}",
            QUrl());
        object.reset(component.create());
    }

    QQmlEngine qml;
    std::unique_ptr<QObject> object;
};

inline void writeConstant(const std::filesystem::path& path, core::Frames frames, float value) {
    const std::vector<float> samples(static_cast<std::size_t>(frames), value);
    writeWav(path, {samples}, kRate);
}

}  // namespace emerick::app::test
