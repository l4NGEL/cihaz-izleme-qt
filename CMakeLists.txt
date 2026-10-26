cmake_minimum_required(VERSION 3.22)
project(cihaz_izleme_qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CIHAZ_BUILD_QT "Qt katmanını, uygulamaları ve Qt testlerini derle" ON)
option(CIHAZ_BUILD_TESTS "Testleri derle" ON)
option(CIHAZ_WERROR "Uyarıları hata say" OFF)
set(CIHAZ_SANITIZER "" CACHE STRING "Boş, 'thread' ya da 'address' (GCC/Clang)")

add_library(cihaz_warnings INTERFACE)
if(MSVC)
  # /utf-8: kaynaklardaki Türkçe metinler kod sayfasına göre bozulmasın
  target_compile_options(cihaz_warnings INTERFACE /W4 /permissive- /utf-8)
  if(CIHAZ_WERROR)
    target_compile_options(cihaz_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(cihaz_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                                  -Wno-sign-conversion)
  if(CIHAZ_WERROR)
    target_compile_options(cihaz_warnings INTERFACE -Werror)
  endif()
endif()

if(CIHAZ_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
elseif(CIHAZ_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

if(CIHAZ_BUILD_QT)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network SerialPort SerialBus Concurrent Widgets
                                           Qml Quick QuickControls2 Charts Test)
  qt_standard_project_setup()
  add_subdirectory(qt)
  add_subdirectory(app)
  add_subdirectory(tools)
endif()

if(CIHAZ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
