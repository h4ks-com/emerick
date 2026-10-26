#pragma once

#include <QWindow>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emerick/engine/clap_instrument.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The live CLAP instrument of every track whose slot names one, with its editor window. A track keeps its instrument
// while its slot names the same plugin, so the plugin keeps the state the user gave it since the project loaded.
class Instruments {
public:
    Instruments() = default;
    ~Instruments();
    Instruments(const Instruments&) = delete;
    Instruments& operator=(const Instruments&) = delete;
    Instruments(Instruments&&) = delete;
    Instruments& operator=(Instruments&&) = delete;

    // Creates the instruments new slots name, loading their saved state, and drops the ones no slot names any more.
    void sync(const model::Document& document);
    void attach(engine::Session& session) const;
    [[nodiscard]] std::shared_ptr<engine::ClapInstrument> find(const model::TrackId& id) const;
    // Why a track with a slot plays no instrument, such as a plugin that is not installed.
    [[nodiscard]] const std::vector<engine::SessionError>& errors() const { return errors_; }
    // Runs each plugin's main-thread requests and follows its editor; true when a plugin marked its state changed.
    bool idle();
    // The document with each slot's state read from its live instrument, as a project saves it.
    [[nodiscard]] model::Document withStates(model::Document document) const;
    // Opens the plugin's own editor window, or a window that embeds its editor. Throws AppError when it has none.
    void showEditor(const model::TrackId& id, const std::string& title);
    [[nodiscard]] bool editorOpen(const model::TrackId& id) const;

private:
    struct Live {
        model::PluginSlot slot;
        std::shared_ptr<engine::ClapInstrument> instrument;
        // Set while the instrument is missing or its state did not load.
        std::string error;
        std::unique_ptr<QWindow> window;
    };

    static void closeEditor(Live& live);

    std::map<std::string, Live> live_;
    std::vector<engine::SessionError> errors_;
};

}  // namespace emerick::app
