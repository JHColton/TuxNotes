cmake_minimum_required(VERSION 3.21)
project(tuxnotes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets DBus)

qt_standard_project_setup()

qt_add_executable(tuxnotes
    src/main.cpp
    src/application.cpp
    src/notewindow.cpp
    src/notestore.cpp
    src/kwinhelper.cpp
)

qt_add_resources(tuxnotes "kwin_scripts" PREFIX "/" FILES
    kwin/tuxnotes-apply.js
    kwin/tuxnotes-watch.js
    kwin/tuxnotes-reconcile.js
)

qt_add_resources(tuxnotes "icons" PREFIX "/" FILES icon.png)

target_link_libraries(tuxnotes PRIVATE Qt6::Gui Qt6::Widgets Qt6::DBus)

include(GNUInstallDirs)
install(TARGETS tuxnotes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.jhc.TuxNotes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES icon.png RENAME tuxnotes.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
