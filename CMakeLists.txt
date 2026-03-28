cmake_minimum_required(VERSION 3.16)

project(MiChat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appMiChat
    main.cpp
    src/ChatBackend.h
    src/ChatBackend.cpp
    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/ChatSession.h
    src/core/ChatSession.cpp
    src/core/SessionManager.h
    src/core/SessionManager.cpp
    src/core/SSEParser.h
    src/core/SSEParser.cpp
    src/core/LLMProvider.h
    src/core/LLMProvider.cpp
    src/core/ClaudeProvider.h
    src/core/ClaudeProvider.cpp
    src/core/OpenAIProvider.h
    src/core/OpenAIProvider.cpp
)

target_include_directories(appMiChat PRIVATE src)

qt_add_qml_module(appMiChat
    URI MiChat
    QML_FILES
        Main.qml
        qml/ChatPage.qml
        qml/SettingsPage.qml
        qml/MessageBubble.qml
        qml/SessionDrawer.qml
        qml/SettingsField.qml
    RESOURCES
        icons/send.svg
        icons/stop.svg
        icons/add.svg
        icons/close.svg
        icons/menu.svg
        icons/settings.svg
        icons/ico.png
)

if(ANDROID)
    include(D:/ProgramData/Android/Sdk/android_openssl/android_openssl.cmake)
endif()

set_target_properties(appMiChat PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
)

if(ANDROID)
    add_android_openssl_libraries(appMiChat)
endif()

target_link_libraries(appMiChat
    PRIVATE Qt6::Quick Qt6::Network
)

install(TARGETS appMiChat
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
