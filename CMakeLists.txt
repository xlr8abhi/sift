cmake_minimum_required(VERSION 3.21)
project(sift LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(sift
  src/main.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/KeyValueTable.h
  src/KeyValueTable.cpp
  src/HistoryStore.h
  src/HistoryStore.cpp
  src/HistoryList.h
  src/HistoryList.cpp
  src/SettingsWidgets.h
  src/SettingsWidgets.cpp
  src/DocTabBar.h
  src/DocTabBar.cpp
  src/BuiltRequest.h
  src/CurlParser.h
  src/CurlParser.cpp
  src/RequestPreview.h
  src/RequestPreview.cpp
  src/RequestTab.h
  src/RequestTab.cpp
  src/FoldingEdit.h
  src/FoldingEdit.cpp
  src/HeadersPane.h
  src/HeadersPane.cpp
  src/ResponseWidgets.h
  src/ResponseWidgets.cpp
  src/HtmlHighlighter.h
  src/HtmlHighlighter.cpp
  src/JsonFormat.h
  src/JsonFormat.cpp
  src/JsonHighlighter.h
  src/JsonHighlighter.cpp
  src/Theme.h
  src/Theme.cpp
  src/SideRail.h
  src/SideRail.cpp
  src/Style.h
  src/Style.cpp
  src/AppFont.h
  src/AppFont.cpp
  src/ThemePicker.h
  src/ThemePicker.cpp
)

target_link_libraries(sift PRIVATE Qt6::Widgets Qt6::Network)

install(TARGETS sift RUNTIME DESTINATION bin)
install(FILES data/sift.desktop DESTINATION share/applications)
install(FILES data/sift.svg DESTINATION share/icons/hicolor/scalable/apps)
