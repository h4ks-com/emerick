#include <QtEnvironmentVariables>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "emerick/broadcast/plugin.hpp"
#include "fuzz_rig.hpp"

using namespace emerick;

namespace {

struct BroadcastFuzzRig : app::test::FuzzRig {
    BroadcastFuzzRig() : broadcasts(broadcast::registerWith(registry, *controller, nav)) {
        qputenv("FAKE_FFMPEG_DIR", dir.path().toUtf8());
        qputenv("FAKE_FFMPEG_MODE", "die");
        broadcasts->useFfmpeg(FAKE_FFMPEG);
        registry.setSetting(broadcast::kEnabledSetting, true);
    }

    std::unique_ptr<broadcast::Broadcasts> broadcasts;
};

}  // namespace

TEST_CASE("every broadcast action survives hostile params and fails without touching the document") {
    app::test::checkHostileParams<BroadcastFuzzRig>([](const std::string& id) { return id.starts_with("broadcast."); });
}
