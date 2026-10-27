#pragma once

#include <QWindow>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
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

    // Creates the instruments new slots name, loading their saved state, and drops the ones no slot names any more. A
    // kept instrument loads its slot's state when the state changed since it last loaded or set it, as after an undo.
    void sync(const model::Document& document);
    void attach(engine::Session& session) const;
    [[nodiscard]] std::shared_ptr<engine::ClapInstrument> find(const model::TrackId& id) const;
    // Why a track with a slot plays no instrument, such as a plugin that is not installed.
    [[nodiscard]] const std::vector<engine::SessionError>& errors() const { return errors_; }
    // Runs each plugin's main-thread requests and follows its editor; true when a plugin marked its state changed.
    bool idle();
    // True once after a plugin changed its params or their values itself.
    bool takeParamsChanged() { return std::exchange(paramsChanged_, false); }
    // The document with each slot's state read from its live instrument, as a project saves it.
    [[nodiscard]] model::Document withStates(model::Document document) const;
    // Opens the plugin's own editor window, or a window that embeds its editor. Throws AppError when it has none.
    void showEditor(const model::TrackId& id, const std::string& title);
    [[nodiscard]] bool editorOpen(const model::TrackId& id) const;
    // The track's instrument as the inspector shows it: whether it has an editor, and each param with its value, its
    // label and, for a stepped param, the label of every step. Null when the track plays no live instrument.
    [[nodiscard]] nlohmann::json describe(const model::TrackId& id) const;
    // Sets the param on the live instrument and returns the state the instrument saves with it. Throws AppError for a
    // missing instrument or param and for a value out of its range.
    std::vector<std::uint8_t> setParam(const model::TrackId& id, std::uint32_t param, double value);

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
    bool paramsChanged_ = false;
};

}  // namespace emerick::app
