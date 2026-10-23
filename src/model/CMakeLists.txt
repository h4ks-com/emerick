add_library(emerick_model
    automation.cpp
    clip_edit.cpp
    command.cpp
    document.cpp
    json.cpp
    midi_notes.cpp
    project.cpp)
target_include_directories(emerick_model PUBLIC include)
target_link_libraries(emerick_model PUBLIC emerick_core nlohmann_json::nlohmann_json)
emerick_target(emerick_model)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_model_tests tests/automation_test.cpp tests/clip_edit_test.cpp tests/midi_notes_test.cpp tests/model_test.cpp)
    target_link_libraries(emerick_model_tests PRIVATE emerick_model)
endif()
