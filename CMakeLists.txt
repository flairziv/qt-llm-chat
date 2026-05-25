cmake_minimum_required(VERSION 3.14)
project(LLMChat VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network WebSockets Multimedia Svg)

set(SOURCES
    main.cpp
    src/core/LLMProvider.cpp
    src/core/ClaudeProvider.cpp
    src/core/OpenAIProvider.cpp
    src/core/GeminiProvider.cpp
    src/core/SSEParser.cpp
    src/core/ChatSession.cpp
    src/core/SessionManager.cpp
    src/core/AppSettings.cpp
    src/core/EdgeTTSProvider.cpp
    src/core/ToolRegistry.cpp
    src/core/tools/FileSystemTools.cpp
    src/core/tools/HttpFetchTool.cpp
    src/ui/MainWindow.cpp
    src/ui/ChatPage.cpp
    src/ui/AnalyticsPage.cpp
    src/ui/AsciiArtPage.cpp
    src/ui/MessageBubble.cpp
    src/ui/SessionListWidget.cpp
    src/ui/SettingClaudePage.cpp
    src/ui/SettingGeminiPage.cpp
    src/ui/SettingOpenAIPage.cpp
    src/ui/SettingGeneralPage.cpp
    src/ui/TachieWindow.cpp
    src/ui/AssistantLoadingWidget.cpp
    src/ui/ShimmerWidget.cpp
    src/ui/ImageViewerDialog.cpp
)

set(HEADERS
    src/core/LLMProvider.h
    src/core/ClaudeProvider.h
    src/core/OpenAIProvider.h
    src/core/GeminiProvider.h
    src/core/NetworkRequestUtils.h
    src/core/SSEParser.h
    src/core/ChatSession.h
    src/core/SessionManager.h
    src/core/AppSettings.h
    src/core/EdgeTTSProvider.h
    src/core/Tool.h
    src/core/ToolRegistry.h
    src/core/tools/FileSystemTools.h
    src/core/tools/HttpFetchTool.h
    src/ui/MainWindow.h
    src/ui/ChatPage.h
    src/ui/AnalyticsPage.h
    src/ui/AsciiArtPage.h
    src/ui/MessageBubble.h
    src/ui/SessionListWidget.h
    src/ui/SettingClaudePage.h
    src/ui/SettingGeminiPage.h
    src/ui/SettingOpenAIPage.h
    src/ui/SettingGeneralPage.h
    src/ui/ProviderSettingHelpers.h
    src/ui/TachieWindow.h
    src/ui/AssistantLoadingWidget.h
    src/ui/ShimmerWidget.h
    src/ui/ImageViewerDialog.h
)

set(UI_FILES
    src/ui/SettingClaudePage.ui
    src/ui/SettingGeminiPage.ui
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
    # 移除 CMake 默认追加的 /W3，避免我们再加 /W4 时报 D9025 "正在重写 /W3"。
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

    target_compile_options(LLMChat PRIVATE
        /utf-8
        /W4                          # 高警告等级（默认 /W3）
        # 抑制低价值警告（Qt / 模板代码大量触发但不反映实际问题）
        /wd4100                      # 'identifier': unreferenced formal parameter
        /wd4127                      # conditional expression is constant（QString 等模板内部）
        /wd4189                      # local variable is initialized but not referenced
        /wd4505                      # unreferenced local function has been removed
        /wd4819                      # 源文件含当前代码页无法表示的字符（中文注释）
        # 把 <angle-bracket> 头视为外部（system header），不对其报警告。
        # 需要 VS 2019 16.10+ 支持；老版本会报未知选项但通常仍能编译。
        /external:anglebrackets
        /external:W0
    )
endif()

target_include_directories(LLMChat PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
# 第三方头放到 SYSTEM include，配合 /external:W0 完全屏蔽其内部告警，
# 让 /W4 只对项目自身代码生效。
target_include_directories(LLMChat SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/json
    ${CMAKE_SOURCE_DIR}/third_party/ElaWidgetTools/include
)

target_link_libraries(LLMChat PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Svg
    ${CMAKE_SOURCE_DIR}/third_party/ElaWidgetTools/lib/ElaWidgetTools.lib
)
if(WIN32)
    # SetCurrentProcessExplicitAppUserModelID（main.cpp 里用，让任务栏固定的快捷
    # 方式和运行中进程能正确合并到同一个图标）由 shell32.dll 提供。
    target_link_libraries(LLMChat PRIVATE shell32)
endif()

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

# Copy OpenSSL DLLs (required by Qt5Network for HTTPS/TLS)
add_custom_command(TARGET LLMChat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/openssl/bin/libssl-1_1-x64.dll"
        "$<TARGET_FILE_DIR:LLMChat>/libssl-1_1-x64.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/openssl/bin/libcrypto-1_1-x64.dll"
        "$<TARGET_FILE_DIR:LLMChat>/libcrypto-1_1-x64.dll"
)
