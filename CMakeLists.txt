cmake_minimum_required(VERSION 3.14)
project(LLMChat VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(SOURCES
    main.cpp
    src/core/LLMProvider.cpp
    src/core/ClaudeProvider.cpp
    src/core/OpenAIProvider.cpp
    src/core/SSEParser.cpp
    src/core/ChatSession.cpp
    src/core/SessionManager.cpp
    src/core/AppSettings.cpp
    src/ui/MainWindow.cpp
    src/ui/ChatPage.cpp
    src/ui/MessageBubble.cpp
    src/ui/SessionListWidget.cpp
    src/ui/SettingClaudePage.cpp
    src/ui/SettingOpenAIPage.cpp
    src/ui/SettingGeneralPage.cpp
    src/ui/TachieWindow.cpp
)

set(HEADERS
    src/core/LLMProvider.h
    src/core/ClaudeProvider.h
    src/core/OpenAIProvider.h
    src/core/SSEParser.h
    src/core/ChatSession.h
    src/core/SessionManager.h
    src/core/AppSettings.h
    src/ui/MainWindow.h
    src/ui/ChatPage.h
    src/ui/MessageBubble.h
    src/ui/SessionListWidget.h
    src/ui/SettingClaudePage.h
    src/ui/SettingOpenAIPage.h
    src/ui/SettingGeneralPage.h
    src/ui/TachieWindow.h
)

set(UI_FILES
    src/ui/SettingClaudePage.ui
    src/ui/SettingOpenAIPage.ui
    src/ui/SettingGeneralPage.ui
)

add_executable(LLMChat WIN32
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    resources/resources.qrc
    app.rc
)

# MSVC 默认按系统代码页（936/GBK）解析源文件，
# 含中文注释的 UTF-8 文件会被误读导致编译错误。
# /utf-8 同时设置源文件编码和执行字符集为 UTF-8。
if(MSVC)
    target_compile_options(LLMChat PRIVATE /utf-8)
endif()

target_include_directories(LLMChat PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/json
    ${CMAKE_SOURCE_DIR}/third_party/ElaWidgetTools/include
)

target_link_libraries(LLMChat PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    ${CMAKE_SOURCE_DIR}/third_party/ElaWidgetTools/lib/ElaWidgetTools.lib
)

# Copy ATRI0.3 tachie resources to config directory after build
add_custom_command(TARGET LLMChat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/ATRI0.3"
        "$<TARGET_FILE_DIR:LLMChat>/config/ATRI0.3"
)

# Copy ElaWidgetTools.dll to output directory after build
add_custom_command(TARGET LLMChat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/ElaWidgetTools/bin/ElaWidgetTools.dll"
        "$<TARGET_FILE_DIR:LLMChat>/ElaWidgetTools.dll"
)
