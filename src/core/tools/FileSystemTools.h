#pragma once

/**
 * @brief 注册文件系统类工具（read_file / list_directory）到全局 ToolRegistry
 *
 * 由 main.cpp 在 MainWindow 构造前调用。注册的工具默认 ReadOnly。
 */
void registerFileSystemTools();
