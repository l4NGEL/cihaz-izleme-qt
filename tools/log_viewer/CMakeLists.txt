# Qt Widgets ile kayıt görüntüleyici: tablo modeli + süzgeç + QtConcurrent ile arka plan okuma/dışa aktarma
qt_add_executable(kayit-goruntuleyici
  main.cpp
  MainWindow.h MainWindow.cpp
  LogTableModel.h LogTableModel.cpp
)
set_target_properties(kayit-goruntuleyici PROPERTIES WIN32_EXECUTABLE ON)
target_link_libraries(kayit-goruntuleyici PRIVATE cihaz_core Qt6::Widgets Qt6::Concurrent cihaz_warnings)
