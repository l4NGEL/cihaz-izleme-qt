qt_add_executable(cihaz-cli cli/main.cpp)
set_target_properties(cihaz-cli PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(cihaz-cli PRIVATE cihaz_qt cihaz_warnings)

add_subdirectory(log_viewer)
