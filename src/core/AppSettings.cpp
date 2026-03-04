#include "AppSettings.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>

// ============================================================
// 构造函数 —— 初始化数据目录和配置文件
// ============================================================

/**
 * @brief 初始化应用配置
 *
 * 执行以下步骤：
 * 1. 确定数据存储目录：可执行文件所在目录/config/
 * 2. 创建目录结构（config/ 和 config/sessions/）
 * 3. 打开或创建 settings.ini 配置文件
 * 4. 首次运行时写入默认值，确保 settings.ini 立即生成
 *
 * 最终的磁盘结构：
 *   <可执行文件目录>/config/
 *   ├── settings.ini      ← 应用配置（INI 格式）
 *   └── sessions/          ← 聊天会话存储目录
 *       ├── {uuid1}.json
 *       └── {uuid2}.json
 */
AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    // QCoreApplication::applicationDirPath() → 可执行文件所在目录
    m_dataDir = QCoreApplication::applicationDirPath() + "/config";

    // mkpath 会递归创建所有不存在的父目录，已存在则不做任何操作
    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_dataDir + "/sessions");
    qInfo() << "LLMChat data dir:" << m_dataDir;

    // QSettings 使用 INI 格式存储配置
    // 注意：QSettings 构造时不会创建文件，只有 setValue + sync 后才写入磁盘
    m_settings = new QSettings(m_dataDir + "/settings.ini", QSettings::IniFormat, this);

    // 强制写入一个默认值并同步到磁盘，确保 settings.ini 文件立即创建
    // 这里用 "读取当前值 → 写回" 的方式，不会覆盖用户已有的配置
    m_settings->setValue("General/active_provider",
                         m_settings->value("General/active_provider", "claude").toString());
    m_settings->sync();  // sync() 将内存中的数据刷新到磁盘文件

    // 检查是否有写入错误（如权限不足、路径无效等）
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "QSettings error:" << m_settings->status();
    }
}

// ============================================================
// Claude API 配置
// ============================================================
// INI 文件中的 [Claude] 分组
// 每个 setter 的流程：写入值 → sync 到磁盘 → 发射 settingsChanged 信号
// settingsChanged 信号会触发 MainWindow::onSettingsChanged()，重新创建 Provider

/// API 密钥，格式如 "sk-ant-api03-xxxx"，默认为空
QString AppSettings::claudeApiKey() const { return m_settings->value("Claude/api_key").toString(); }
void AppSettings::setClaudeApiKey(const QString &key) { m_settings->setValue("Claude/api_key", key); m_settings->sync(); emit settingsChanged(); }

/// API 基础 URL，默认 "https://api.anthropic.com"，可改为代理地址
QString AppSettings::claudeBaseUrl() const { return m_settings->value("Claude/base_url", "https://api.anthropic.com").toString(); }
void AppSettings::setClaudeBaseUrl(const QString &url) { m_settings->setValue("Claude/base_url", url); m_settings->sync(); emit settingsChanged(); }

/// 模型名称，如 "claude-opus-4-6"、"claude-sonnet-4-20250514"
QString AppSettings::claudeModel() const { return m_settings->value("Claude/model", "claude-sonnet-4-20250514").toString(); }
void AppSettings::setClaudeModel(const QString &model) { m_settings->setValue("Claude/model", model); m_settings->sync(); emit settingsChanged(); }

// ============================================================
// OpenAI API 配置
// ============================================================
// INI 文件中的 [OpenAI] 分组

/// API 密钥，格式如 "sk-xxxx"，默认为空
QString AppSettings::openaiApiKey() const { return m_settings->value("OpenAI/api_key").toString(); }
void AppSettings::setOpenaiApiKey(const QString &key) { m_settings->setValue("OpenAI/api_key", key); m_settings->sync(); emit settingsChanged(); }

/// API 基础 URL，默认 "https://api.openai.com"，可改为代理地址
QString AppSettings::openaiBaseUrl() const { return m_settings->value("OpenAI/base_url", "https://api.openai.com").toString(); }
void AppSettings::setOpenaiBaseUrl(const QString &url) { m_settings->setValue("OpenAI/base_url", url); m_settings->sync(); emit settingsChanged(); }

/// 模型名称，如 "gpt-4o"、"gpt-4o-mini"
QString AppSettings::openaiModel() const { return m_settings->value("OpenAI/model", "gpt-4o").toString(); }
void AppSettings::setOpenaiModel(const QString &model) { m_settings->setValue("OpenAI/model", model); m_settings->sync(); emit settingsChanged(); }

// ============================================================
// 通用配置
// ============================================================
// INI 文件中的 [General] 分组

/// 当前激活的 API 提供商："claude" 或 "openai"
QString AppSettings::activeProvider() const { return m_settings->value("General/active_provider", "claude").toString(); }
void AppSettings::setActiveProvider(const QString &provider) { m_settings->setValue("General/active_provider", provider); m_settings->sync(); emit settingsChanged(); }

/// 系统提示词，会作为 system message 发送给 API，引导 AI 的行为
QString AppSettings::systemPrompt() const { return m_settings->value("General/system_prompt", "You are a helpful assistant.").toString(); }
void AppSettings::setSystemPrompt(const QString &prompt) { m_settings->setValue("General/system_prompt", prompt); m_settings->sync(); emit settingsChanged(); }

/// 最大输出 token 数，限制 AI 回复长度，默认 4096
int AppSettings::maxTokens() const { return m_settings->value("General/max_tokens", 4096).toInt(); }
void AppSettings::setMaxTokens(int tokens) { m_settings->setValue("General/max_tokens", tokens); m_settings->sync(); emit settingsChanged(); }

/// 温度参数（0.0~2.0），控制输出随机性，默认 0.7
double AppSettings::temperature() const { return m_settings->value("General/temperature", 0.7).toDouble(); }
void AppSettings::setTemperature(double temp) { m_settings->setValue("General/temperature", temp); m_settings->sync(); emit settingsChanged(); }

// ============================================================
// 立绘配置
// ============================================================
// INI 文件中的 [Tachie] 分组

/// 是否启用立绘窗口，默认 true
bool AppSettings::tachieEnabled() const { return m_settings->value("Tachie/enabled", true).toBool(); }
void AppSettings::setTachieEnabled(bool enabled) { m_settings->setValue("Tachie/enabled", enabled); m_settings->sync(); }

/// 立绘窗口 X 坐标，-1 表示使用默认位置
int AppSettings::tachiePositionX() const { return m_settings->value("Tachie/position_x", -1).toInt(); }
void AppSettings::setTachiePositionX(int x) { m_settings->setValue("Tachie/position_x", x); m_settings->sync(); }

/// 立绘窗口 Y 坐标，-1 表示使用默认位置
int AppSettings::tachiePositionY() const { return m_settings->value("Tachie/position_y", -1).toInt(); }
void AppSettings::setTachiePositionY(int y) { m_settings->setValue("Tachie/position_y", y); m_settings->sync(); }

// ============================================================
// 背景图配置
// ============================================================
// INI 文件中的 [Background] 分组
// 路径为空时 MainWindow 使用内置资源文件作为兜底

/// 绘制模式：0=Normal, 1=Pixmap, 2=Movie，默认 2(Movie)
int AppSettings::backgroundPaintMode() const { return m_settings->value("Background/paint_mode", 2).toInt(); }
void AppSettings::setBackgroundPaintMode(int mode) { m_settings->setValue("Background/paint_mode", mode); m_settings->sync(); }

/// 静态背景图本地路径，如 "C:/imgs/bg.png"
QString AppSettings::backgroundPixmapPath() const { return m_settings->value("Background/pixmap_path").toString(); }
void AppSettings::setBackgroundPixmapPath(const QString &path) { m_settings->setValue("Background/pixmap_path", path); m_settings->sync(); }

/// 动态背景图(GIF)本地路径，如 "C:/imgs/bg.gif"
QString AppSettings::backgroundMoviePath() const { return m_settings->value("Background/movie_path").toString(); }
void AppSettings::setBackgroundMoviePath(const QString &path) { m_settings->setValue("Background/movie_path", path); m_settings->sync(); }

// ============================================================
// 数据目录路径
// ============================================================

/// 返回数据存储根目录，供 SessionManager 使用（读写会话 JSON 文件）
QString AppSettings::dataDir() const { return m_dataDir; }
