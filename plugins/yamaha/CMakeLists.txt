add_library(emerick_yamaha_codec
    src/chart.cpp
    src/chord.cpp
    src/keyboard_template.cpp
    src/profile.cpp
    src/render.cpp
    src/sysex.cpp)
target_include_directories(emerick_yamaha_codec PUBLIC include)
target_link_libraries(emerick_yamaha_codec PUBLIC emerick_core nlohmann_json::nlohmann_json)
emerick_target(emerick_yamaha_codec)

qt_add_library(emerick_yamaha_plugin STATIC
    include/emerick/yamaha/chord_input.hpp
    include/emerick/yamaha/plugin.hpp
    src/arranger_actions.cpp
    src/arranger_styles.cpp
    src/arranger_styles.hpp
    src/block_actions.cpp
    src/block_edit.hpp
    src/builtin_arranger.cpp
    src/chart_actions.cpp
    src/chord_input.cpp
    src/keyboard_check.cpp
    src/keyboard_check.hpp
    src/midi_listener.hpp
    src/plugin.cpp
    src/style_sync.cpp
    src/style_sync.hpp
    src/registration.hpp
    src/style_library.cpp
    src/style_library.hpp
    src/style_notes.cpp
    src/style_notes.hpp
    src/template_actions.cpp
    src/voice_list.cpp
    src/voice_list.hpp)
set(yamaha_qml qml/BlockLane.qml qml/ChordLane.qml qml/StyleHeader.qml qml/StyleInspector.qml qml/StyleLane.qml)
foreach(file ${yamaha_qml})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
qt_add_qml_module(emerick_yamaha_plugin
    URI Emerick.Yamaha
    VERSION 1.0
    QML_FILES ${yamaha_qml}
    SOURCES src/block_strip.cpp src/block_strip.hpp src/lane_blocks.cpp src/lane_blocks.hpp)
target_include_directories(emerick_yamaha_plugin PRIVATE src)
target_link_libraries(emerick_yamaha_plugin
    PUBLIC emerick_app emerick_yamaha_codec
    PRIVATE emerick_arranger emerick_gm_sounds emerick_voices_params Qt6::Qml Qt6::Quick libremidi)
emerick_target(emerick_yamaha_plugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_yamaha_codec_tests
        tests/chart_test.cpp
        tests/chord_test.cpp
        tests/keyboard_template_test.cpp
        tests/profile_test.cpp
        tests/render_test.cpp
        tests/sysex_test.cpp)
    target_link_libraries(emerick_yamaha_codec_tests PRIVATE emerick_yamaha_codec libremidi)
    target_compile_definitions(emerick_yamaha_codec_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    emerick_test(emerick_yamaha_plugin_tests
        tests/plugin_test.cpp
        tests/arranger_test.cpp
        tests/builtin_arranger_test.cpp
        tests/hostile_params_test.cpp
        tests/lane_edit_test.cpp
        tests/template_actions_test.cpp
        ${PROJECT_SOURCE_DIR}/src/voices/tests/synthetic_store.cpp)
    target_include_directories(emerick_yamaha_plugin_tests PRIVATE src
        ${PROJECT_SOURCE_DIR}/src/app/tests ${PROJECT_SOURCE_DIR}/src/arranger/tests ${PROJECT_SOURCE_DIR}/src/voices/tests)
    target_link_libraries(emerick_yamaha_plugin_tests PRIVATE emerick_yamaha_plugin libremidi
        emerick_test_clap emerick_gm_clap emerick_voices_clap emerick_arranger_clap emerick_arranger PkgConfig::SNDFILE)
    target_compile_definitions(emerick_yamaha_plugin_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    emerick_test(emerick_yamaha_lanes_tests tests/lanes_test.cpp)
    target_include_directories(emerick_yamaha_lanes_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/app/tests ${PROJECT_SOURCE_DIR}/src/ui/tests ${PROJECT_SOURCE_DIR}/src/arranger/tests)
    target_link_libraries(emerick_yamaha_lanes_tests PRIVATE
        emerick_yamaha_plugin emerick_yamaha_pluginplugin emerick_uiplugin emerick_arranger_clap Qt6::Quick Qt6::Test)
endif()
