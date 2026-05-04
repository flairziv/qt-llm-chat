#include "AppSettings.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QTimer>

namespace {
    // setter→sync/emit 的防抖窗口，单位毫秒。
    // 200ms 经验值：用户连续打字（如逐字输入 API Key）的间隔通常 < 100ms，
    // 选 200ms 能确保完整输入序列只触发一次刷盘 + 信号；超过 200ms
    // 的暂停被视为"用户停下了"，立刻持久化并通知监听者。
    constexpr int FLUSH_DEBOUNCE_MS = 200;
}

// ============================================================
// 构造 / 析构
// ============================================================

/**
 * @brief 初始化数据目录、QSettings 与防抖定时器
 *
 * 流程：
 * 1. 在可执行文件目录下建立 config/ 与 config/sessions/
 * 2. 创建 QSettings（INI 格式）指向 config/settings.ini
 * 3. 强制写一次 active_provider 默认值——目的是触发文件创建，
 *    使首次启动时 settings.ini 就在磁盘上能被用户找到/编辑
 * 4. 创建两个 200ms 单触发定时器，分别绑到 flushProvider / flushUi
 *    （信号分区 + 防抖的实现核心）
 *
 * 最终的磁盘结构：
 *   <可执行文件目录>/config/
 *   ├── settings.ini      → 应用配置（INI 格式）
 *   └── sessions/         → 聊天会话存储目录
 *       ├── {uuid1}.json
 *       └── {uuid2}.json
 */
AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    m_dataDir = QCoreApplication::applicationDirPath() + "/config";
    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_dataDir + "/sessions");
    qInfo() << "LLMChat data dir:" << m_dataDir;

    m_settings = new QSettings(m_dataDir + "/settings.ini", QSettings::IniFormat, this);

    // 强制写一次 active_provider 默认值，确保 settings.ini 立即生成
    m_settings->setValue("General/active_provider",
                         m_settings->value("General/active_provider", "claude").toString());
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "QSettings error:" << m_settings->status();
    }

    m_providerFlushTimer = new QTimer(this);
    m_providerFlushTimer->setSingleShot(true);
    m_providerFlushTimer->setInterval(FLUSH_DEBOUNCE_MS);
    connect(m_providerFlushTimer, &QTimer::timeout, this, &AppSettings::flushProvider);

    m_uiFlushTimer = new QTimer(this);
    m_uiFlushTimer->setSingleShot(true);
    m_uiFlushTimer->setInterval(FLUSH_DEBOUNCE_MS);
    connect(m_uiFlushTimer, &QTimer::timeout, this, &AppSettings::flushUi);
}

/**
 * @brief 析构兜底：未触发的 flush 定时器在程序退出前强制写盘
 *
 * 用户改完一个 setter 立即关闭程序时，定时器尚未到期，原本会丢配置。
 * 这里检查两个 flush 定时器，stop() 后调一次 sync() 落盘。
 *
 * 故意不 emit 信号——程序退出阶段 MainWindow / ChatPage 等监听者
 * 可能已被销毁，emit 后接收槽 lambda 引用的对象都已无效。
 */
AppSettings::~AppSettings()
{
    bool needSync = false;
    if (m_providerFlushTimer && m_providerFlushTimer->isActive()) {
        m_providerFlushTimer->stop();
        needSync = true;
    }
    if (m_uiFlushTimer && m_uiFlushTimer->isActive()) {
        m_uiFlushTimer->stop();
        needSync = true;
    }
    if (needSync && m_settings) {
        m_settings->sync();
    }
}

// ============================================================
// 防抖调度 & flush
// ============================================================

/**
 * @brief 调度一次 Provider 配置变更刷盘
 *
 * 由 Provider 类 setter（setClaudeApiKey 等）调用。
 * QTimer::start() 在已经 active 的定时器上会重置剩余时间——
 * 标准防抖语义：连续调用多次只在最后一次的 200ms 后触发 flushProvider 一次。
 */
void AppSettings::scheduleProviderFlush() { m_providerFlushTimer->start(); }

/**
 * @brief 调度一次 UI 配置变更刷盘
 *
 * 由 UI 类 setter（setUserName / setSystemPrompt 等）调用，行为同 scheduleProviderFlush。
 */
void AppSettings::scheduleUiFlush()       { m_uiFlushTimer->start(); }

/**
 * @brief Provider 配置 flush 槽函数
 *
 * 把累积的 setValue 一次性写入磁盘，并通知 MainWindow 重建 Provider。
 * 由 m_providerFlushTimer 触发——意味着用户停止输入 200ms 后才会执行。
 */
void AppSettings::flushProvider()
{
    m_settings->sync();
    emit providerSettingsChanged();
}

/**
 * @brief UI 配置 flush 槽函数
 *
 * 同 flushProvider，但发的是 uiSettingsChanged，由监听者做 UI 刷新（不重建 Provider）。
 */
void AppSettings::flushUi()
{
    m_settings->sync();
    emit uiSettingsChanged();
}

// ============================================================
// Claude API（变更触发 providerSettingsChanged）
// ============================================================

QString AppSettings::claudeApiKey() const { return m_settings->value("Claude/api_key").toString(); }
void AppSettings::setClaudeApiKey(const QString &key) { m_settings->setValue("Claude/api_key", key); scheduleProviderFlush(); }

QString AppSettings::claudeBaseUrl() const { return m_settings->value("Claude/base_url", "https://api.anthropic.com").toString(); }
void AppSettings::setClaudeBaseUrl(const QString &url) { m_settings->setValue("Claude/base_url", url); scheduleProviderFlush(); }

QString AppSettings::claudeModel() const { return m_settings->value("Claude/model", "claude-sonnet-4-20250514").toString(); }
void AppSettings::setClaudeModel(const QString &model) { m_settings->setValue("Claude/model", model); scheduleProviderFlush(); }

QString AppSettings::claudeReasoningEffort() const { return m_settings->value("Claude/reasoning_effort").toString(); }
void AppSettings::setClaudeReasoningEffort(const QString &effort) { m_settings->setValue("Claude/reasoning_effort", effort); scheduleProviderFlush(); }

// ============================================================
// OpenAI API（变更触发 providerSettingsChanged）
// ============================================================

QString AppSettings::openaiApiKey() const { return m_settings->value("OpenAI/api_key").toString(); }
void AppSettings::setOpenaiApiKey(const QString &key) { m_settings->setValue("OpenAI/api_key", key); scheduleProviderFlush(); }

QString AppSettings::openaiBaseUrl() const { return m_settings->value("OpenAI/base_url", "https://api.openai.com").toString(); }
void AppSettings::setOpenaiBaseUrl(const QString &url) { m_settings->setValue("OpenAI/base_url", url); scheduleProviderFlush(); }

QString AppSettings::openaiModel() const { return m_settings->value("OpenAI/model", "gpt-4o").toString(); }
void AppSettings::setOpenaiModel(const QString &model) { m_settings->setValue("OpenAI/model", model); scheduleProviderFlush(); }

QString AppSettings::openaiReasoningEffort() const { return m_settings->value("OpenAI/reasoning_effort").toString(); }
void AppSettings::setOpenaiReasoningEffort(const QString &effort) { m_settings->setValue("OpenAI/reasoning_effort", effort); scheduleProviderFlush(); }

// ============================================================
// Gemini API（变更触发 providerSettingsChanged）
// ============================================================

QString AppSettings::geminiApiKey() const { return m_settings->value("Gemini/api_key").toString(); }
void AppSettings::setGeminiApiKey(const QString &key) { m_settings->setValue("Gemini/api_key", key); scheduleProviderFlush(); }

QString AppSettings::geminiBaseUrl() const { return m_settings->value("Gemini/base_url", "https://generativelanguage.googleapis.com").toString(); }
void AppSettings::setGeminiBaseUrl(const QString &url) { m_settings->setValue("Gemini/base_url", url); scheduleProviderFlush(); }

QString AppSettings::geminiModel() const { return m_settings->value("Gemini/model", "gemini-2.5-flash").toString(); }
void AppSettings::setGeminiModel(const QString &model) { m_settings->setValue("Gemini/model", model); scheduleProviderFlush(); }

// ============================================================
// 通用配置
// ============================================================

// active_provider 不发任何信号——MainWindow::onProviderSwitched 已显式调 createProvider，
// 若再发 providerSettingsChanged 会形成 createProvider→setActiveProvider→重建的死循环。
QString AppSettings::activeProvider() const { return m_settings->value("General/active_provider", "claude").toString(); }
void AppSettings::setActiveProvider(const QString &provider) { m_settings->setValue("General/active_provider", provider); m_settings->sync(); }

static const char *DEFAULT_SYSTEM_PROMPT =
    "你是一个可爱的桌面伙伴，通过一个带有语音和立绘的聊天应用与用户交流。\n"
    "回复要简洁自然、口语化，像朋友之间聊天一样。\n"
    "避免冗长的列表和格式化文本，适合语音朗读的短句更好。";

// systemPrompt / maxTokens / temperature 在每次发请求时读取，不重建 Provider，
// 但归入 uiFlush 走防抖（用户在 spinbox / textarea 里频繁修改时减少刷盘）。
QString AppSettings::systemPrompt() const { return m_settings->value("General/system_prompt", DEFAULT_SYSTEM_PROMPT).toString(); }
void AppSettings::setSystemPrompt(const QString &prompt) { m_settings->setValue("General/system_prompt", prompt); scheduleUiFlush(); }

int AppSettings::maxTokens() const { return m_settings->value("General/max_tokens", 4096).toInt(); }
void AppSettings::setMaxTokens(int tokens) { m_settings->setValue("General/max_tokens", tokens); scheduleUiFlush(); }

double AppSettings::temperature() const { return m_settings->value("General/temperature", 0.7).toDouble(); }
void AppSettings::setTemperature(double temp) { m_settings->setValue("General/temperature", temp); scheduleUiFlush(); }

// ============================================================
// 立绘配置（无信号；调用方需要时自行处理）
// ============================================================

bool AppSettings::tachieEnabled() const { return m_settings->value("Tachie/enabled", true).toBool(); }
void AppSettings::setTachieEnabled(bool enabled) { m_settings->setValue("Tachie/enabled", enabled); m_settings->sync(); }

int AppSettings::tachiePositionX() const { return m_settings->value("Tachie/position_x", -1).toInt(); }
void AppSettings::setTachiePositionX(int x) { m_settings->setValue("Tachie/position_x", x); m_settings->sync(); }

int AppSettings::tachiePositionY() const { return m_settings->value("Tachie/position_y", -1).toInt(); }
void AppSettings::setTachiePositionY(int y) { m_settings->setValue("Tachie/position_y", y); m_settings->sync(); }

// ============================================================
// TTS 配置
// ============================================================

bool AppSettings::ttsEnabled() const { return m_settings->value("TTS/enabled", false).toBool(); }
void AppSettings::setTtsEnabled(bool enabled) { m_settings->setValue("TTS/enabled", enabled); m_settings->sync(); }

QString AppSettings::ttsVoice() const { return m_settings->value("TTS/voice", "zh-CN-XiaoxiaoNeural").toString(); }
void AppSettings::setTtsVoice(const QString &voice) { m_settings->setValue("TTS/voice", voice); m_settings->sync(); }

// ============================================================
// 角色名（变更触发 uiSettingsChanged → ChatPage 更新气泡）
// ============================================================

QString AppSettings::userName() const { return m_settings->value("General/user_name", "You").toString(); }
void AppSettings::setUserName(const QString &name) { m_settings->setValue("General/user_name", name); scheduleUiFlush(); }

QString AppSettings::assistantName() const { return m_settings->value("General/assistant_name", "Assistant").toString(); }
void AppSettings::setAssistantName(const QString &name) { m_settings->setValue("General/assistant_name", name); scheduleUiFlush(); }

// ============================================================
// Prompt 模板（独立信号）
// ============================================================

static const QStringList DEFAULT_PROMPT_TEMPLATES = {
    "Translate to English",
    "Translate to Chinese",
    "Summarize",
    "Explain Code",
    "Fix Bug",
    "Optimize Code"
};

QStringList AppSettings::promptTemplates() const
{
    QStringList t = m_settings->value("PromptTemplates/list").toStringList();
    return t.isEmpty() ? DEFAULT_PROMPT_TEMPLATES : t;
}

void AppSettings::setPromptTemplates(const QStringList &templates)
{
    m_settings->setValue("PromptTemplates/list", templates);
    m_settings->sync();
    emit promptTemplatesChanged();
}

// ============================================================
// 立绘资源目录（变更触发 uiSettingsChanged → MainWindow 更新立绘窗口）
// ============================================================

QString AppSettings::tachieResourceDir() const { return m_settings->value("Tachie/resource_dir", "ATRI0.3").toString(); }
void AppSettings::setTachieResourceDir(const QString &dir) { m_settings->setValue("Tachie/resource_dir", dir); scheduleUiFlush(); }

// ============================================================
// 背景图（无信号）
// ============================================================

int AppSettings::backgroundPaintMode() const { return m_settings->value("Background/paint_mode", 2).toInt(); }
void AppSettings::setBackgroundPaintMode(int mode) { m_settings->setValue("Background/paint_mode", mode); m_settings->sync(); }

QString AppSettings::backgroundPixmapPath() const { return m_settings->value("Background/pixmap_path").toString(); }
void AppSettings::setBackgroundPixmapPath(const QString &path) { m_settings->setValue("Background/pixmap_path", path); m_settings->sync(); }

QString AppSettings::backgroundMoviePath() const { return m_settings->value("Background/movie_path").toString(); }
void AppSettings::setBackgroundMoviePath(const QString &path) { m_settings->setValue("Background/movie_path", path); m_settings->sync(); }

// ============================================================
// 数据目录路径
// ============================================================

QString AppSettings::dataDir() const { return m_dataDir; }
