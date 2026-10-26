# Uygulama, araçlar ve Qt testlerinin paylaştığı, arayüzden bağımsız Qt katmanı
add_library(cihaz_qt STATIC
  src/transport/ByteTransport.h
  src/transport/SerialTransport.h src/transport/SerialTransport.cpp
  src/transport/TcpTransport.h src/transport/TcpTransport.cpp
  src/transport/UdpTransport.h src/transport/UdpTransport.cpp
  src/transport/TransportFactory.h src/transport/TransportFactory.cpp
  src/can/CanLink.h src/can/CanLink.cpp
  src/modbus/ModbusMaster.h src/modbus/ModbusMaster.cpp
  src/session/SessionTypes.h
  src/session/DeviceSession.h src/session/DeviceSession.cpp
  src/session/Acquisition.h src/session/Acquisition.cpp
)
target_include_directories(cihaz_qt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cihaz_qt
  PUBLIC cihaz_core Qt6::Core Qt6::Network Qt6::SerialPort Qt6::SerialBus
  PRIVATE cihaz_warnings)
