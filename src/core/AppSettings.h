#pragma once
#include <QObject>
#include <QSettings>
#include <QString>

/**
 * @brief 应用配置管理类
 *
 * 封装 QSettings，负责读写 settings.ini 配置文件。
 * 管理三类配置：Claude API、OpenAI API、通用设置。
 *
 * 设计模式：
 * - 每个配置项提供 getter/setter 对
 * - setter 内部执行：setValue → sync（写入磁盘）→ emit settingsChanged
 * - getter 提供默认值，首次读取时无需预先初始化
 *
 * 信号连接关系：
 *   settingsChanged → MainWindow::onSettingsChanged → 重新创建 LLMProvider
 *   （确保修改 API Key 或模型后，下次请求使用新配置）
 *
 * 配置文件路径：<可执行文件目录>/config/settings.ini
 * 文件格式示例：
 *   [Claude]
 *   api_key=sk-ant-xxxx
 *   base_url=https://api.anthropic.com
 *   model=claude-opus-4-6
 *
 *   [OpenAI]
 *   api_key=sk-xxxx
 *   base_url=https://api.openai.com
 *   model=gpt-4o
 *
 *   [General]
 *   active_provider=claude
 *   system_prompt=You are a helpful assistant.
 *   max_tokens=4096
 *   temperature=0.7
 */
class AppSettings : public QObject
{
    Q_OBJECT
public:
    explicit AppSettings(QObject *parent = nullptr);

    // --- Claude API 配置 ---
    QString claudeApiKey() const;               // API 密钥
    void setClaudeApiKey(const QString &key);
    QString claudeBaseUrl() const;              // API 基础 URL（可配置代理）
    void setClaudeBaseUrl(const QString &url);
    QString claudeModel() const;                // 妯″瀷鍚嶇О锛屽 "claude-opus-4-6"
    void setClaudeModel(const QString &model);
    QString claudeReasoningEffort() const;      // 鎺ㄧ悊寮哄害锛屽 "low"銆?"medium"銆?"high"
    void setClaudeReasoningEffort(const QString &effort);

    // --- OpenAI API 配置 ---
    QString openaiApiKey() const;               // API 密钥
    void setOpenaiApiKey(const QString &key);
    QString openaiBaseUrl() const;              // API 基础 URL（可配置代理）
    void setOpenaiBaseUrl(const QString &url);
    QString openaiModel() const;                // 妯″瀷鍚嶇О锛屽 "gpt-4o"
    void setOpenaiModel(const QString &model);
    QString openaiReasoningEffort() const;      // 鎺ㄧ悊寮哄害锛屽 "low"銆?"medium"銆?"high"
    void setOpenaiReasoningEffort(const QString &effort);

    // --- 通用配置 ---
    QString activeProvider() const;             // 当前激活的提供商："claude" 或 "openai"
    void setActiveProvider(const QString &provider);
    QString systemPrompt() const;               // 系统提示词，引导 AI 行为
    void setSystemPrompt(const QString &prompt);
    int maxTokens() const;                      // 最大输出 token 数（默认 4096）
    void setMaxTokens(int tokens);
    double temperature() const;                 // 温度参数 0.0~2.0（默认 0.7）
    void setTemperature(double temp);

    // --- 立绘配置 ---
    bool tachieEnabled() const;                 // 是否启用立绘窗口（默认 true）
    void setTachieEnabled(bool enabled);
    int tachiePositionX() const;                // 立绘窗口 X 坐标（默认 -1 表示自动）
    void setTachiePositionX(int x);
    int tachiePositionY() const;                // 立绘窗口 Y 坐标（默认 -1 表示自动）
    void setTachiePositionY(int y);

    // --- TTS 配置 ---
    bool ttsEnabled() const;                    // 是否启用 TTS 语音朗读（默认 false）
    void setTtsEnabled(bool enabled);
    QString ttsVoice() const;                   // TTS 语音名称（默认 "zh-CN-XiaoxiaoNeural"）
    void setTtsVoice(const QString &voice);

    // --- 背景图配置 ---
    int backgroundPaintMode() const;            // 绘制模式：0=Normal, 1=Pixmap, 2=Movie（默认 2）
    void setBackgroundPaintMode(int mode);
    QString backgroundPixmapPath() const;       // 静态背景图本地路径（空则用内置资源）
    void setBackgroundPixmapPath(const QString &path);
    QString backgroundMoviePath() const;        // 动态背景图(GIF)本地路径（空则用内置资源）
    void setBackgroundMoviePath(const QString &path);

    // --- 数据目录 ---
    QString dataDir() const;                    // 返回数据根目录，供 SessionManager 使用

signals:
    /**
     * @brief 任意配置项被修改时发射
     *
     * MainWindow 连接此信号，收到后调用 createProvider() 重建 API 客户端，
     * 确保后续请求使用最新的 API Key、模型等配置。
     */
    void settingsChanged();

private:
    QSettings *m_settings;  // Qt 配置读写对象（INI 格式）
    QString m_dataDir;      // 数据存储根目录（<可执行文件目录>/config/）
};
