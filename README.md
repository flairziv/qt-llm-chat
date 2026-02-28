# LLMChat

一个基于 Qt 的桌面端大语言模型聊天客户端，支持 Claude (Anthropic) 和 OpenAI 双 API，具备流式响应、多会话管理、立绘表情联动等功能。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Qt](https://img.shields.io/badge/Qt-5.15-green)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![License](https://img.shields.io/badge/License-MIT-yellow)

## 功能特性

- **双 API 支持** — 支持 Claude (Anthropic) 和 OpenAI 两种 API，一键切换
- **SSE 流式响应** — 实时逐字显示 AI 回复，体验流畅
- **多会话管理** — 创建、切换、删除独立对话，会话持久化到本地 JSON
- **立绘表情联动** — 独立立绘窗口根据 AI 回复的情绪自动切换表情，支持淡入淡出和 5 种动画效果
- **系统托盘** — 最小化到托盘，后台常驻
- **可配置** — API Key、Base URL、模型、System Prompt、温度等参数均可自定义
- **[ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) 美化** — 基于 ElaWidgetTools 的 FluentUI 风格现代化界面

## 截图

<!-- 在这里添加你的应用截图 -->
<!-- ![主界面](screenshots/main.png) -->
<!-- ![立绘联动](screenshots/tachie.png) -->

## 项目结构

```
LLMChat/
├── main.cpp                          # 程序入口
├── CMakeLists.txt                    # CMake 构建配置
├── src/
│   ├── core/                         # 核心业务层
│   │   ├── LLMProvider.h/cpp         #   LLM 抽象基类（策略模式）
│   │   ├── ClaudeProvider.h/cpp      #   Claude API 实现
│   │   ├── OpenAIProvider.h/cpp      #   OpenAI API 实现
│   │   ├── SSEParser.h/cpp           #   SSE 协议解析器
│   │   ├── ChatSession.h/cpp         #   会话数据模型
│   │   ├── SessionManager.h/cpp      #   多会话管理器
│   │   └── AppSettings.h/cpp         #   配置管理（INI 格式）
│   └── ui/                           # UI 表现层
│       ├── MainWindow.h/cpp          #   主窗口（调度中心）
│       ├── ChatPage.h/cpp            #   聊天页面
│       ├── MessageBubble.h/cpp       #   消息气泡控件
│       ├── SessionListWidget.h/cpp   #   会话列表控件
│       ├── SettingClaudePage.h/cpp/ui #  Claude 设置页
│       ├── SettingOpenAIPage.h/cpp/ui #  OpenAI 设置页
│       ├── SettingGeneralPage.h/cpp/ui # 通用设置页
│       └── TachieWindow.h/cpp        #   立绘窗口
├── resources/
│   ├── resources.qrc                 # Qt 资源文件
│   └── ATRI0.3/                      # 立绘资源（表情 PNG + 动画配置）
└── third_party/
    ├── json/                         # JSON 库
    └── ElaWidgetTools/               # UI 美化库
```

## 架构设计

```
┌─────────────────────────────────────────────────┐
│  MainWindow（调度中心）                           │
│                                                 │
│  ChatPage ←─信号槽─→ MainWindow ←──→ LLMProvider │
│                          ↕                      │
│                   SessionManager                │
│                          ↕                      │
│                SettingXxxPage（设置页）            │
│                          ↕                      │
│                 TachieWindow（立绘联动）           │
└─────────────────────────────────────────────────┘
```

**核心设计模式**：
- **策略模式** — `LLMProvider` 抽象基类，`ClaudeProvider` / `OpenAIProvider` 子类实现不同 API
- **信号槽** — Qt 信号槽机制实现模块间解耦通信
- **MVC 变体** — Model（ChatSession）、View（ChatPage）、Controller（MainWindow）

## 环境要求

- **Qt** 5.15+
- **CMake** 3.14+
- **编译器** MSVC 2019+（64-bit）
- **ElaWidgetTools** — 已包含在 `third_party/` 中

## 构建步骤

```bash
# 1. 克隆仓库
git clone https://github.com/你的用户名/LLMChat.git
cd LLMChat

# 2. 创建构建目录
mkdir build && cd build

# 3. CMake 配置（请确保 Qt 在 PATH 中或指定 CMAKE_PREFIX_PATH）
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="你的Qt路径/5.15.2/msvc2019_64" ..

# 4. 编译
cmake --build . --config Release
```

> 注意：ElaWidgetTools 提供的是 Release 版本的 DLL，请务必使用 **Release** 模式编译。

## 使用说明

1. 首次启动后，进入 **Settings → Claude / OpenAI** 页面填写 API Key
2. 可选修改 Base URL（支持代理/中转地址）
3. 选择模型后回到 **Chat** 页面即可开始对话
4. 立绘窗口会根据 AI 回复的语气自动切换表情
5. `Ctrl+T` 切换立绘窗口显示/隐藏

## 数据存储

所有数据保存在可执行文件目录下的 `config/` 文件夹中：

```
config/
├── settings.ini          # 应用配置
├── sessions/             # 聊天记录（JSON 格式）
│   ├── {uuid1}.json
│   └── {uuid2}.json
└── ATRI0.3/              # 立绘资源
```

## 技术栈

| 技术 | 用途 |
|------|------|
| C++17 | 开发语言 |
| Qt 5.15 Widgets | GUI 框架 |
| CMake | 构建系统 |
| [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) | UI 美化（FluentUI 风格无边框窗口、导航栏、主题） |
| SSE (Server-Sent Events) | LLM API 流式响应协议 |
| QPropertyAnimation | 立绘过渡与动画效果 |

## 致谢

- [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) — 由 [Liniyous](https://github.com/Liniyous) 开发的 Qt FluentUI 风格组件库，为本项目提供了美观的 UI 基础

## License

MIT License
