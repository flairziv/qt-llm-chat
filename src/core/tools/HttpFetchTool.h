#pragma once

/**
 * @brief 注册 fetch_url 工具到全局 ToolRegistry
 *
 * 由 main.cpp 在 MainWindow 构造前调用。当前默认 ReadOnly 跑通协议链路；
 * 后续 commit 会把级别改为 ShellOrNetwork 并接入审批弹窗。
 */
void registerHttpFetchTool();
