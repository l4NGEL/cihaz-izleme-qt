find_package(GTest QUIET)
if(NOT GTest_FOUND)
  # Windows'ta sistem GoogleTest'i yok; kaynaktan derlenir
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(cihaz_core_tests
  core/test_crc.cpp
  core/test_frame_codec.cpp
  core/test_modbus.cpp
  core/test_can_signal.cpp
  core/test_device_map.cpp
  core/test_sample_store.cpp
  core/test_log_file.cpp
  core/test_stream_decoder.cpp
)
target_link_libraries(cihaz_core_tests PRIVATE cihaz_core cihaz_warnings GTest::gtest_main
                                               Threads::Threads)
gtest_discover_tests(cihaz_core_tests DISCOVERY_TIMEOUT 30)

if(CIHAZ_BUILD_QT)
  add_subdirectory(qt)
endif()
