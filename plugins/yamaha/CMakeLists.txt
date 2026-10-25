add_library(emerick_yamaha_codec
    src/chart.cpp
    src/chord.cpp
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
    src/chart_actions.cpp
    src/chord_input.cpp
    src/keyboard_check.cpp
    src/keyboard_check.hpp
    src/plugin.cpp
    src/registration.hpp)
set(yamaha_qml qml/BlockLane.qml qml/ChordLane.qml qml/StyleHeader.qml qml/StyleLane.qml qml/WarningBadge.qml)
foreach(file ${yamaha_qml} qml/warning.svg)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
qt_add_qml_module(emerick_yamaha_plugin
    URI Emerick.Yamaha
    VERSION 1.0
    QML_FILES ${yamaha_qml}
    RESOURCES qml/warning.svg
    SOURCES src/lane_blocks.cpp src/lane_blocks.hpp)
target_include_directories(emerick_yamaha_plugin PRIVATE src)
target_link_libraries(emerick_yamaha_plugin
    PUBLIC emerick_app emerick_yamaha_codec
    PRIVATE Qt6::Qml libremidi)
emerick_target(emerick_yamaha_plugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_yamaha_codec_tests
        tests/chart_test.cpp
        tests/chord_test.cpp
        tests/profile_test.cpp
        tests/render_test.cpp
        tests/sysex_test.cpp)
    target_link_libraries(emerick_yamaha_codec_tests PRIVATE emerick_yamaha_codec libremidi)
    target_compile_definitions(emerick_yamaha_codec_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    emerick_test(emerick_yamaha_plugin_tests
        tests/plugin_test.cpp
        tests/arranger_test.cpp
        tests/hostile_params_test.cpp
        tests/lane_edit_test.cpp)
    target_include_directories(emerick_yamaha_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_yamaha_plugin_tests PRIVATE emerick_yamaha_plugin libremidi)
    target_compile_definitions(emerick_yamaha_plugin_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

    emerick_test(emerick_yamaha_lanes_tests tests/lanes_test.cpp)
    target_include_directories(emerick_yamaha_lanes_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/app/tests ${PROJECT_SOURCE_DIR}/src/ui/tests)
    target_link_libraries(emerick_yamaha_lanes_tests PRIVATE
        emerick_yamaha_plugin emerick_yamaha_pluginplugin emerick_uiplugin Qt6::Quick Qt6::Test)
endif()
