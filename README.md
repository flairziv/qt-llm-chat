# MiChat

基于 Qt 6 QML 和 C++ 开发的 Android AI 聊天应用，支持 OpenAI 兼容接口和 Claude API。

## 功能特性

- 流式输出（SSE 实时响应）
- 多会话管理，支持历史记录
- 支持 OpenAI 兼容接口（OpenAI、DeepSeek 等）和 Anthropic Claude
- 设置本地持久化（QSettings）
- Material Design 风格 UI，针对 Android 优化

## 环境要求

- Qt 6.5+
- Android SDK / NDK
- Android OpenSSL（`android_openssl`）
- CMake 3.16+

## 编译方式

### Android 调试版

1. 安装 [android_openssl](https://github.com/KDAB/android_openssl) 到 `D:/ProgramData/Android/Sdk/android_openssl`
2. 用 Qt Creator 打开 `CMakeLists.txt`
3. 选择 Android arm64-v8a Kit
4. 点击 **构建 → 运行 CMake**
5. 编译并部署到设备

### Release 签名打包

1. 生成签名密钥库：
   ```bash
   keytool -genkey -v -keystore michat.keystore -alias michat -keyalg RSA -keysize 2048 -validity 10000
   ```
2. Qt Creator 中：**项目 → Android → 构建 → 为 APK 签名**
3. 填写密钥库路径、别名和密码
4. 选择 **Release** 构建类型后编译

> 注意：不要将 `michat.keystore` 提交到 git

## 使用配置

点击应用右上角齿轮图标进入设置页面：

| 字段 | 说明 |
|------|------|
| Provider | 选择 `openai` 或 `claude` |
| API Key | 你的 API 密钥 |
| Base URL | 接口地址（如 `https://api.openai.com/v1`） |
| Model | 模型名称（如 `gpt-4o`、`claude-opus-4-5`） |

设置会自动保存，重启后自动恢复。

## 项目结构

```
MiChat/
├── main.cpp
├── Main.qml
├── qml/
│   ├── ChatPage.qml        # 主聊天页面
│   ├── SettingsPage.qml    # 设置页面
│   ├── SettingsField.qml   # 设置输入组件
│   ├── MessageBubble.qml   # 消息气泡组件
│   └── SessionDrawer.qml   # 历史会话抽屉
├── src/
│   ├── ChatBackend.h / .cpp
│   └── core/
│       ├── AppSettings     # 设置持久化
│       ├── ChatSession     # 单次会话数据
│       ├── SessionManager  # 会话管理
│       ├── SSEParser       # 流式响应解析
│       ├── LLMProvider     # 接口基类
│       ├── ClaudeProvider  # Claude 实现
│       └── OpenAIProvider  # OpenAI 实现
├── android/
│   ├── AndroidManifest.xml
│   └── res/mipmap-*/ic_launcher.png
└── icons/
```

## 开源协议

MIT
