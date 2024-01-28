add_library(manager STATIC
    qt_manager_mainwindow.h qt_manager_mainwindow.cpp qt_manager_mainwindow.ui
    qt_manager_vmadddialog.h qt_manager_vmadddialog.cpp qt_manager_vmadddialog.ui
    qt_manager_settings.h qt_manager_settings.cpp qt_manager_settings.ui


)
target_include_directories(manager PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../" "${CMAKE_CURRENT_SOURCE_DIR}/")
target_link_libraries(manager PRIVATE
    Qt${QT_MAJOR}::Widgets
    Qt${QT_MAJOR}::Gui
    Qt${QT_MAJOR}::Network
    Threads::Threads
)
target_link_libraries(plat PUBLIC manager)
target_link_libraries(manager PRIVATE plat ui)
