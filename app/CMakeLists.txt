qt_add_executable(emerick MACOSX_BUNDLE main.cpp)
emerick_target(emerick)
target_link_libraries(emerick PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick emerick_app emerick_uiplugin)
