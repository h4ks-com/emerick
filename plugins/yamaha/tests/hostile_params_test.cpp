#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "emerick/yamaha/plugin.hpp"
#include "fuzz_rig.hpp"

using namespace emerick;

namespace {

struct YamahaFuzzRig : app::test::FuzzRig {
    YamahaFuzzRig() : listeners(yamaha::registerWith(registry, *controller, nav)) {}

    void populate() {
        FuzzRig::populate();
        for (const char* kind : {"yamaha.chord", "yamaha.style"}) {
            model::Track track;
            track.kind = kind;
            track.name = kind;
            controller->project().addTrack(std::move(track));
        }
    }

    std::unique_ptr<yamaha::Listeners> listeners;
};

}  // namespace

TEST_CASE("every Yamaha action survives hostile params and fails without touching the document") {
    app::test::checkHostileParams<YamahaFuzzRig>([](const std::string& id) { return id.starts_with("yamaha."); });
}
