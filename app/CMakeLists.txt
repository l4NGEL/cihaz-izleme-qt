qt_add_executable(cihaz-izleme
  src/main.cpp
  src/AppController.h src/AppController.cpp
  src/ChannelModel.h src/ChannelModel.cpp
  src/EventLogModel.h src/EventLogModel.cpp
  src/PlotFeeder.h src/PlotFeeder.cpp
)
set_target_properties(cihaz-izleme PROPERTIES WIN32_EXECUTABLE ON)
target_include_directories(cihaz-izleme PRIVATE src)
target_link_libraries(cihaz-izleme PRIVATE
  cihaz_qt Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Charts Qt6::Concurrent
  cihaz_warnings)

qt_add_resources(cihaz-izleme "qml"
  PREFIX "/"
  FILES
    qml/Main.qml
    qml/Theme.js
    qml/Card.qml
    qml/ConnectionBar.qml
    qml/ChannelPanel.qml
    qml/PlotCard.qml
    qml/ControlPanel.qml
    qml/StatsPanel.qml
    qml/EventPanel.qml
)

install(TARGETS cihaz-izleme RUNTIME DESTINATION .)
