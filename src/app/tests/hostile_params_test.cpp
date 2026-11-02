#include <catch2/catch_test_macros.hpp>
#include <string>

#include "fuzz_rig.hpp"

using namespace emerick::app::test;

TEST_CASE("every action survives hostile params and fails without touching the document") {
    const QTemporaryDir folder;
    const std::filesystem::path previous = std::filesystem::current_path();
    // We run from a scratch folder, so the files that relative paths in the params name land there.
    std::filesystem::current_path(folder.path().toStdString());
    checkHostileParams<FuzzRig>([](const std::string&) { return true; });
    std::filesystem::current_path(previous);
}

TEST_CASE("every required param of an action is listed in its schema's properties") {
    const FuzzRig rig;
    for (const emerick::app::Action& action : rig.registry.actions()) {
        if (!action.schema.is_object()) {
            continue;
        }
        const auto properties = action.schema.value("properties", emerick::app::Json::object());
        for (const std::string& required : action.schema.value("required", std::vector<std::string>{})) {
            INFO(action.id + " requires " + required);
            CHECK(properties.contains(required));
        }
    }
}
