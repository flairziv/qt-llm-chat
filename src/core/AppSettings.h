#pragma once
#include <QObject>
#include <QSettings>
#include <QString>

class QTimer;

/**
 * @brief 应用配置管理类
 *
 * 封装 QSettings，负责读写 settings.ini 配置文件。
 * 管理：Claude / OpenAI / Gemini API、立绘 / TTS / 通用设置。
 *
 * 设计要点：
 * - 每个配置项提供 getter / setter 对，getter 自带默认值
 * - setter 写入后通过 200ms 防抖批量 sync 并发出分区信号
 * - 信号分两类：
 *     providerSettingsChanged → MainWindow 重建 LLMProvider
 *     uiSettingsChanged       → MainWindow 刷新角色名 / 立绘资源目录等
 *   连续输入 API Key、改用户名等只会触发一次刷盘 + 一次通知。
 *
 * 配置文件路径：<可执行文件目录>/config/settings.ini
 */
class AppSettings : public QObject
{
    Q_OBJECT
public:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings();

    // --- Claude API ---
    QString claudeApiKey() const;
    void setClaudeApiKey(const QString &key);
    QString claudeBaseUrl() const;
    void setClaudeBaseUrl(const QString &url);
    QString claudeModel() const;
    void setClaudeModel(const QString &model);
    QString claudeReasoningEffort() const;
    void setClaudeReasoningEffort(const QString &effort);

    // --- OpenAI API ---
    QString openaiApiKey() const;
    void setOpenaiApiKey(const QString &key);
    QString openaiBaseUrl() const;
    void setOpenaiBaseUrl(const QString &url);
    QString openaiModel() const;
    void setOpenaiModel(const QString &model);
    QString openaiReasoningEffort() const;
    void setOpenaiReasoningEffort(const QString &effort);
    QString openaiImageApiMode() const;          // "images" / "edits" / "chat" / "responses"
    void setOpenaiImageApiMode(const QString &mode);

    // --- Gemini API ---
    QString geminiApiKey() const;
    void setGeminiApiKey(const QString &key);
    QString geminiBaseUrl() const;
    void setGeminiBaseUrl(const QString &url);
    QString geminiModel() const;
    void setGeminiModel(const QString &model);

    // --- 通用配置 ---
    QString activeProvider() const;              // "claude" / "openai" / "gemini"
    void setActiveProvider(const QString &provider);
    QString systemPrompt() const;                // 系统提示词（每次请求读取，不重建 Provider）
    void setSystemPrompt(const QString &prompt);
    int maxTokens() const;
    void setMaxTokens(int tokens);
    double temperature() const;
    void setTemperature(double temp);

    // --- 工具调用配置 ---
    // 无信号：请求构造（ClaudeProvider）/ 审批门（MainWindow）在用到时实时读取，
    // 改这些不重建 Provider，也就不会打断在途流式。
    bool toolsEnabled() const;                              // 工具总开关，默认 true
    void setToolsEnabled(bool enabled);
    bool toolEnabled(const QString &name) const;            // 单个工具开关，默认 true
    void setToolEnabled(const QString &name, bool enabled);
    QString toolRiskOverride(const QString &name) const;    // 风险级别覆盖，空=用注册默认
    void setToolRiskOverride(const QString &name, const QString &risk);

    // --- 立绘配置 ---
    bool tachieEnabled() const;
    void setTachieEnabled(bool enabled);
    int tachiePositionX() const;
    void setTachiePositionX(int x);
    int tachiePositionY() const;
    void setTachiePositionY(int y);

    // --- TTS 配置 ---
    bool ttsEnabled() const;
    void setTtsEnabled(bool enabled);
    QString ttsVoice() const;
    void setTtsVoice(const QString &voice);

    // --- 角色名 ---
    QString userName() const;
    void setUserName(const QString &name);
    QString assistantName() const;
    void setAssistantName(const QString &name);

    // --- Prompt 模板 ---
    QStringList promptTemplates() const;
    void setPromptTemplates(const QStringList &templates);

    // --- 立绘资源目录 ---
    QString tachieResourceDir() const;
    void setTachieResourceDir(const QString &dir);

    // --- 背景图配置 ---
    int backgroundPaintMode() const;             // 0=Normal, 1=Pixmap, 2=Movie（默认 2）
    void setBackgroundPaintMode(int mode);
    QString backgroundPixmapPath() const;
    void setBackgroundPixmapPath(const QString &path);
    QString backgroundMoviePath() const;
    void setBackgroundMoviePath(const QString &path);

    // --- 数据目录 ---
    QString dataDir() const;

signals:
    /**
     * @brief Provider 相关配置（API Key / baseUrl / model / 推理强度 / 图像端点）变更
     *
     * MainWindow 收到后调 createProvider() 重建 LLMProvider。
     * 通过 200ms 防抖发出：用户在文本框里逐字输入 API Key 时只刷盘并通知一次。
     */
    void providerSettingsChanged();

    /**
     * @brief 影响 UI 显示的轻量设置（角色名、立绘资源目录、系统提示词等）变更
     *
     * MainWindow 收到后做 UI 刷新（更新气泡角色名、立绘窗口资源目录），
     * 不会触发 Provider 重建。同样有 200ms 防抖。
     */
    void uiSettingsChanged();

    /** @brief Prompt 模板列表被修改（独立信号，由 ChatPage 监听） */
    void promptTemplatesChanged();

private:
    /**
     * @brief 调度一次 200ms 后的批量 sync + emit
     *
     * 单触发定时器，每次调用都会重置剩余时间——典型防抖语义：用户停下输入后
     * 200ms 才落盘并通知。析构时若仍在排队会兜底刷一次盘。
     */
    void scheduleProviderFlush();
    void scheduleUiFlush();
    void flushProvider();
    void flushUi();

    QSettings *m_settings;  // Qt 配置读写对象（INI 格式）
    QString m_dataDir;      // 数据存储根目录（<可执行文件目录>/config/）

    QTimer *m_providerFlushTimer = nullptr;
    QTimer *m_uiFlushTimer = nullptr;
};
