#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "fuzz_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct Edit {
    std::string id;
    Json params;
};

using EditMaker = std::function<std::optional<Edit>(const model::Document&, std::mt19937&)>;

std::optional<std::string> anyTrack(const model::Document& document, std::mt19937& rng, const char* kind = nullptr) {
    std::vector<std::string> ids;
    for (const model::Track& track : document.tracks) {
        if (kind == nullptr || track.kind == kind) {
            ids.push_back(track.id.value);
        }
    }
    return ids.empty() ? std::nullopt : std::optional{pick(ids, rng)};
}

std::optional<std::string> anyClip(const model::Document& document, std::mt19937& rng) {
    std::vector<std::string> ids;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            ids.push_back(model::clipId(clip).value);
        }
    }
    return ids.empty() ? std::nullopt : std::optional{pick(ids, rng)};
}

std::optional<std::string> anyMarker(const model::Document& document, std::mt19937& rng) {
    if (document.markers.empty()) {
        return std::nullopt;
    }
    return pick(document.markers, rng).id.value;
}

core::Ticks anyTicks(std::mt19937& rng) { return std::uniform_int_distribution<core::Ticks>{0, 16 * 960}(rng); }

EditMaker onTrack(const char* id, const std::function<Json(std::mt19937&)>& params, const char* kind = nullptr) {
    return [=](const model::Document& document, std::mt19937& rng) -> std::optional<Edit> {
        const auto track = anyTrack(document, rng, kind);
        if (!track) {
            return std::nullopt;
        }
        Json given = params(rng);
        given["track"] = *track;
        return Edit{id, given};
    };
}

EditMaker onClip(const char* id, const std::function<Json(std::mt19937&)>& params) {
    return [=](const model::Document& document, std::mt19937& rng) -> std::optional<Edit> {
        const auto clip = anyClip(document, rng);
        if (!clip) {
            return std::nullopt;
        }
        Json given = params(rng);
        given["clip"] = *clip;
        return Edit{id, given};
    };
}

EditMaker onMarker(const char* id, const std::function<Json(std::mt19937&)>& params) {
    return [=](const model::Document& document, std::mt19937& rng) -> std::optional<Edit> {
        const auto marker = anyMarker(document, rng);
        if (!marker) {
            return std::nullopt;
        }
        Json given = params(rng);
        given["marker"] = *marker;
        return Edit{id, given};
    };
}

std::vector<EditMaker> editMakers(const std::string& wav) {
    const auto none = [](std::mt19937&) { return Json::object(); };
    const auto flag = [](std::mt19937& rng) { return Json{{"value", std::bernoulli_distribution{}(rng)}}; };
    const auto db = [](std::mt19937& rng) { return Json{{"db", std::uniform_real_distribution{-60.0, 12.0}(rng)}}; };
    return {
        [](const model::Document&, std::mt19937&) { return std::optional{Edit{"track.addAudio", Json::object()}}; },
        [](const model::Document&, std::mt19937&) { return std::optional{Edit{"track.addMidi", Json::object()}}; },
        [](const model::Document&, std::mt19937& rng) {
            constexpr std::array kDenominators{1, 2, 4, 8, 16};
            return std::optional{
                Edit{"project.setTempo",
                     {{"bpm", std::uniform_real_distribution{20.0, 400.0}(rng)},
                      {"numerator", std::uniform_int_distribution{1, 12}(rng)},
                      {"denominator", kDenominators.at(std::uniform_int_distribution<std::size_t>{0, 4}(rng))}}}};
        },
        onTrack("track.rename",
                [](std::mt19937& rng) { return Json{{"name", "Take " + std::to_string(rng() % 100)}}; }),
        onTrack("track.setMute", flag),
        onTrack("track.setSolo", flag),
        onTrack("track.setGain", db),
        onTrack("track.setColor", [](std::mt19937& rng) { return Json{{"color", static_cast<int>(rng() % 8)}}; }),
        onTrack("track.duplicate", none),
        onTrack("track.remove", none),
        [](const model::Document& document, std::mt19937& rng) -> std::optional<Edit> {
            const auto track = anyTrack(document, rng);
            if (!track) {
                return std::nullopt;
            }
            return Edit{"track.move", {{"track", *track}, {"index", rng() % document.tracks.size()}}};
        },
        onTrack(
            "clip.import", [wav](std::mt19937& rng) { return Json{{"path", wav}, {"at", anyTicks(rng)}}; }, "audio"),
        onClip("clip.move", [](std::mt19937& rng) { return Json{{"start", anyTicks(rng)}}; }),
        onClip("clip.split", [](std::mt19937& rng) { return Json{{"at", anyTicks(rng)}}; }),
        onClip("clip.resize", [](std::mt19937& rng) { return Json{{"length", 1 + anyTicks(rng)}}; }),
        onClip("clip.setFade", [](std::mt19937& rng) { return Json{{"fadeIn", anyTicks(rng) / 8}}; }),
        onClip("clip.setGain", db),
        onClip("clip.duplicate", none),
        onClip("clip.delete", none),
        [](const model::Document&, std::mt19937& rng) {
            return std::optional{Edit{"marker.add", {{"at", anyTicks(rng)}, {"name", "Verse"}}}};
        },
        onMarker("marker.rename",
                 [](std::mt19937& rng) { return Json{{"name", "Part " + std::to_string(rng() % 10)}}; }),
        onMarker("marker.move", [](std::mt19937& rng) { return Json{{"at", anyTicks(rng)}}; }),
        onMarker("marker.setColor", [](std::mt19937& rng) { return Json{{"color", static_cast<int>(rng() % 8)}}; }),
        onMarker("marker.delete", none),
    };
}

// Undoes every step through the registry, the way a person or an agent would, and counts them.
int undoAll(const ProjectRig& rig) {
    int steps = 0;
    while (rig.controller->project().undoName()) {
        rig.run("project.undo");
        ++steps;
    }
    return steps;
}

int redoAll(const ProjectRig& rig) {
    int steps = 0;
    while (rig.controller->project().redoName()) {
        rig.run("project.redo");
        ++steps;
    }
    return steps;
}

}  // namespace

TEST_CASE("random edits each leave one undo step, undo back to the start and redo to the end") {
    const std::uint32_t seed = fuzzSeed();
    INFO("EMERICK_SEED=" << seed);
    std::mt19937 rng{seed};
    constexpr int kEdits = 120;
    for (const bool populated : {false, true}) {
        FuzzRig rig;
        if (populated) {
            rig.populate();
            rig.controller->project().reset(rig.controller->project().document());
        }
        writeConstant(rig.path("take.wav"), 24000, 0.5F);
        const std::vector<EditMaker> makers = editMakers(rig.path("take.wav").string());
        const model::Document start = rig.controller->project().document();
        int changes = 0;
        for (int step = 0; step < kEdits; ++step) {
            const auto edit = pick(makers, rng)(rig.controller->project().document(), rng);
            if (!edit) {
                continue;
            }
            INFO("edit " << step << ": " << edit->id << " " << edit->params.dump());
            const model::Document before = rig.controller->project().document();
            const auto undoBefore = rig.controller->project().undoName();
            try {
                rig.registry.run(edit->id, edit->params, ActionSource::Mcp);
            } catch (const AppError& error) {
                INFO(error.what());
                REQUIRE(rig.controller->project().document() == before);
                continue;
            }
            const model::Document after = rig.controller->project().document();
            if (after == before) {
                REQUIRE(rig.controller->project().undoName() == undoBefore);
                continue;
            }
            ++changes;
            rig.run("project.undo");
            REQUIRE(rig.controller->project().document() == before);
            rig.run("project.redo");
            REQUIRE(rig.controller->project().document() == after);
        }
        const model::Document end = rig.controller->project().document();
        CHECK(undoAll(rig) == changes);
        CHECK(rig.controller->project().document() == start);
        CHECK(redoAll(rig) == changes);
        CHECK(rig.controller->project().document() == end);
    }
}
