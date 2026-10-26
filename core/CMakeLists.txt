# Qt'ye bağımlı olmayan çekirdek: protokoller, örnek tamponu, kayıt dosyası.
# Ayrı tutulduğu için sanitizer'larla ve Qt kurulu olmayan makinede test edilebilir.
add_library(cihaz_core STATIC
  src/crc.cpp
  src/frame_codec.cpp
  src/modbus.cpp
  src/can_signal.cpp
  src/device_map.cpp
  src/sample_store.cpp
  src/log_file.cpp
  src/stream_decoder.cpp
  src/csv_export.cpp
)
target_include_directories(cihaz_core PUBLIC include)
target_link_libraries(cihaz_core PRIVATE cihaz_warnings)
target_compile_options(cihaz_core PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
