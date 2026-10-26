function(cihaz_qt_test name)
  qt_add_executable(${name} ${name}.cpp FakeTransport.h)
  set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE OFF)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE cihaz_qt Qt6::Test cihaz_warnings)
  add_test(NAME ${name} COMMAND ${name})
  # Ekransız CI'da QtGui eklentisi aranmasın
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endfunction()

cihaz_qt_test(tst_transports)
cihaz_qt_test(tst_modbusmaster)
cihaz_qt_test(tst_canlink)
cihaz_qt_test(tst_session)
if(UNIX AND NOT APPLE)
  target_link_libraries(tst_transports PRIVATE util)  # openpty
endif()

# Uçtan uca: Python cihaz benzeticisi + cihaz-cli (gerçek süreçler, gerçek soket/pty)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME benzetici_birim
           COMMAND ${Python3_EXECUTABLE} -m unittest -v test_sim
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/sim)
  add_test(NAME uctan_uca
           COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/e2e/e2e_test.py
                   --cli $<TARGET_FILE:cihaz-cli> --sim ${PROJECT_SOURCE_DIR}/sim/msd200_sim.py)
  set_tests_properties(uctan_uca PROPERTIES TIMEOUT 240 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
