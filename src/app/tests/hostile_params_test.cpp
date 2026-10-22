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
