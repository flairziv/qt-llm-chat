#include "SettingOpenAIPage.h"
#include "ui_SettingOpenAIPage.h"
#include "ProviderSettingHelpers.h"
#include "core/AppSettings.h"

namespace {

// imageApiMode 持久化为四选一枚举（"images" / "edits" / "chat" / "responses"），
// 但旧配置 / 手改 INI 可能写成 "response" / "edit" / "images_edits" / 大小写变体——
// 写回前在这里归一化一次，避免 combo 找不到匹配项导致回写跑偏。
QString normalizeImageApiMode(const QString &raw)
{
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("chat")) return QStringLiteral("chat");
    if (s == QLatin1String("responses") || s == QLatin1String("response")) return QStringLiteral("responses");
    if (s == QLatin1String("edits") || s == QLatin1String("edit") || s == QLatin1String("images_edits")) return QStringLiteral("edits");
    return QStringLiteral("images");
}

// 加载时也用同样规则归一化已存值，确保 combo 能匹配上
QString loadImageApiMode(AppSettings *s)
{
    return normalizeImageApiMode(s->openaiImageApiMode());
}

} // namespace

SettingOpenAIPage::SettingOpenAIPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingOpenAIPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);

    // 允许手填模型名（支持 Ollama 等本地模型如 "llama3.2"）
    ui->comboBox_model->setEditable(true);

    using namespace ProviderSettingHelpers;
    AppSettings *s = m_settings;

    bindLineEdit(ui->lineEdit_apiKey,
                 [s] { return s->openaiApiKey(); },
                 [s](const QString &t) { s->setOpenaiApiKey(t); });
    bindLineEdit(ui->lineEdit_baseUrl,
                 [s] { return s->openaiBaseUrl(); },
                 [s](const QString &t) { s->setOpenaiBaseUrl(t); });
    bindComboBox(ui->comboBox_model,
                 [s] { return s->openaiModel(); },
                 [s](const QString &t) { s->setOpenaiModel(t); });
    bindReasoningComboBox(ui->comboBox_reasoning,
                          [s] { return s->openaiReasoningEffort(); },
                          [s](const QString &t) { s->setOpenaiReasoningEffort(t); });
    bindComboBox(ui->comboBox_imageApiMode,
                 [s] { return loadImageApiMode(s); },
                 [s](const QString &t) { s->setOpenaiImageApiMode(t); },
                 &normalizeImageApiMode);
}

SettingOpenAIPage::~SettingOpenAIPage()
{
    delete ui;
}
