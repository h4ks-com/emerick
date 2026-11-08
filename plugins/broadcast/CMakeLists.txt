qt_add_library(emerick_broadcast_plugin STATIC
    include/emerick/broadcast/plugin.hpp
    src/broadcaster.cpp
    src/broadcaster.hpp
    src/plugin.cpp
    src/profile.cpp
    src/profile.hpp)
set(broadcast_qml qml/BroadcastPanel.qml qml/LiveBadge.qml)
foreach(file ${broadcast_qml})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
qt_add_qml_module(emerick_broadcast_plugin
    URI Emerick.Broadcast
    VERSION 1.0
    QML_FILES ${broadcast_qml})
target_include_directories(emerick_broadcast_plugin PUBLIC include PRIVATE src)
target_link_libraries(emerick_broadcast_plugin PUBLIC emerick_app PRIVATE Qt6::Network Qt6::Qml)
emerick_target(emerick_broadcast_plugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_broadcast_tests tests/broadcast_test.cpp)
    target_include_directories(emerick_broadcast_tests PRIVATE src ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_broadcast_tests PRIVATE emerick_broadcast_plugin Qt6::Network)
    target_compile_definitions(emerick_broadcast_tests PRIVATE
        FAKE_FFMPEG="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-ffmpeg.sh")
endif()
