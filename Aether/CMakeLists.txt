cmake_minimum_required(VERSION 3.16)

project(Aether VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Network Widgets)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appAether
    src/main.cpp
    src/core/AppCore.h src/core/AppCore.cpp
    src/database/DatabaseWorker.h src/database/DatabaseWorker.cpp
    src/network/NetworkWorker.h src/network/NetworkWorker.cpp
    src/models/ContactsModel.h src/models/ContactsModel.cpp
    src/models/MessagesModel.h src/models/MessagesModel.cpp
    src/app_icon.rc
)

target_include_directories(appAether PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

qt_add_qml_module(appAether
    URI Aether
    QML_FILES
        Main.qml
    RESOURCES
        icons/settings.png
        icons/add.png
        icons/edit.png
        icons/delete.png
        icons/close.png
        icons/attach.png
        icons/clock.png
        icons/logo.png
        icons/send.png
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appAether PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appAether
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(appAether
    PRIVATE Qt6::Quick
            Qt6::Sql
            Qt6::Network
            Qt6::Widgets
)

install(TARGETS appAether
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
