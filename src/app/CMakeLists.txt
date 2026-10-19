qt_add_library(emerick_app STATIC
    action_params.cpp
    audio_file.cpp
    backends.cpp
    clip_actions.cpp
    clip_edit_actions.cpp
    clip_notes.cpp
    core_actions.cpp
    export_actions.cpp
    include/emerick/app/action_params.hpp
    include/emerick/app/audio_file.hpp
    include/emerick/app/backends.hpp
    include/emerick/app/clip_notes.hpp
    include/emerick/app/core_actions.hpp
    include/emerick/app/enums.hpp
    include/emerick/app/event_wait.hpp
    include/emerick/app/introspector.hpp
    include/emerick/app/lua_host.hpp
    include/emerick/app/mcp_server.hpp
    include/emerick/app/mcp_tools.hpp
    include/emerick/app/midi_export.hpp
    include/emerick/app/nav.hpp
    include/emerick/app/params.hpp
    include/emerick/app/peak_cache.hpp
    include/emerick/app/project_actions.hpp
    include/emerick/app/project_bundle.hpp
    include/emerick/app/project_controller.hpp
    include/emerick/app/project_state.hpp
    include/emerick/app/qml_exposure.hpp
    include/emerick/app/registry.hpp
    include/emerick/app/session_mapping.hpp
    include/emerick/app/stem_export.hpp
    include/emerick/app/track_list_model.hpp
    include/emerick/app/transport_clock.hpp
    include/emerick/app/ui_api.hpp
    introspector.cpp
    lua_host.cpp
    mcp_server.cpp
    mcp_tools.cpp
    midi_export.cpp
    peak_cache.cpp
    project_actions.cpp
    project_bundle.cpp
    project_controller.cpp
    project_file_actions.cpp
    project_state.cpp
    qml_exposure.cpp
    registry.cpp
    session_mapping.cpp
    stem_export.cpp
    track_actions.cpp
    track_edit_actions.cpp
    track_list_model.cpp
    transport_clock.cpp
    ui_api.cpp)
target_include_directories(emerick_app PUBLIC include)
target_compile_definitions(emerick_app PRIVATE EMERICK_VERSION="${PROJECT_VERSION}" SOL_PRINT_ERRORS=0)
target_link_libraries(emerick_app
    PRIVATE lua54 sol2::sol2 PkgConfig::SNDFILE libremidi
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::HttpServer emerick_core emerick_model emerick_engine
        nlohmann_json::nlohmann_json)
emerick_target(emerick_app)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_app_tests
        tests/devices_test.cpp
        tests/introspector_test.cpp
        tests/lua_host_test.cpp
        tests/mcp_server_test.cpp
        tests/peak_cache_test.cpp
        tests/project_file_test.cpp
        tests/project_test.cpp
        tests/recording_test.cpp
        tests/registry_test.cpp
        tests/transport_test.cpp)
    target_link_libraries(emerick_app_tests
        PRIVATE emerick_app Qt6::Network Qt6::Qml Qt6::QuickControls2 PkgConfig::SNDFILE libremidi)
endif()
