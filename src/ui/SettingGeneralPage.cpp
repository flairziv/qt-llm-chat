#include "SettingGeneralPage.h"
#include "ui_SettingGeneralPage.h"
#include "core/AppSettings.h"
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <ElaRadioButton.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaLineEdit.h>
#include <ElaWindow.h>
#include <ElaToggleSwitch.h>
#include <ElaComboBox.h>

SettingGeneralPage::SettingGeneralPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingGeneralPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    setupRoleNameSection();
    setupPromptTemplatesSection();
    setupBackgroundSection();
    setupTTSSection();
    loadSettings();
    connectAutoSave();
}

SettingGeneralPage::~SettingGeneralPage()
{
    delete ui;
}

/**
 * @brief 构建 "Role Names" 设置区域
 *
 * 包含两个输入框：User Name / Assistant Name，
 * 修改后自动保存并发射 settingsChanged 信号，
 * MainWindow 收到后刷新所有气泡标签。
 */
void SettingGeneralPage::setupRoleNameSection()
{
    QVBoxLayout *layout = ui->verticalLayout_2;
    int insertIdx = layout->indexOf(ui->noteLabel);

    // --- Section Header ---
    ElaText *sectionRole = new ElaText("Role Names", this);
    QFont boldFont;
    boldFont.setBold(true);
    sectionRole->setFont(boldFont);
    layout->insertWidget(insertIdx++, sectionRole);

    // --- User Name ---
    ElaScrollPageArea *userArea = new ElaScrollPageArea(this);
    QHBoxLayout *userLayout = new QHBoxLayout(userArea);
    userLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *userLabel = new ElaText("User Name", this);
    userLabel->setTextPixelSize(15);
    userLayout->addWidget(userLabel);
    userLayout->addStretch();

    m_userNameEdit = new ElaLineEdit(this);
    m_userNameEdit->setFixedWidth(200);
    m_userNameEdit->setText(m_settings->userName());
    userLayout->addWidget(m_userNameEdit);

    connect(m_userNameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setUserName(text);
        emit settingsChanged();
    });

    layout->insertWidget(insertIdx++, userArea);

    // --- Assistant Name ---
    ElaScrollPageArea *assistantArea = new ElaScrollPageArea(this);
    QHBoxLayout *assistantLayout = new QHBoxLayout(assistantArea);
    assistantLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *assistantLabel = new ElaText("Assistant Name", this);
    assistantLabel->setTextPixelSize(15);
    assistantLayout->addWidget(assistantLabel);
    assistantLayout->addStretch();

    m_assistantNameEdit = new ElaLineEdit(this);
    m_assistantNameEdit->setFixedWidth(200);
    m_assistantNameEdit->setText(m_settings->assistantName());
    assistantLayout->addWidget(m_assistantNameEdit);

    connect(m_assistantNameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setAssistantName(text);
        emit settingsChanged();
    });

    layout->insertWidget(insertIdx++, assistantArea);
}

/**
 * @brief 构建 "Prompt Templates" 设置区域
 *
 * 使用 QPlainTextEdit，每行一条模板。保存时按换行拆分。
 */
void SettingGeneralPage::setupPromptTemplatesSection()
{
    QVBoxLayout *layout = ui->verticalLayout_2;
    int insertIdx = layout->indexOf(ui->noteLabel);

    ElaText *section = new ElaText("Prompt Templates", this);
    QFont boldFont; boldFont.setBold(true);
    section->setFont(boldFont);
    layout->insertWidget(insertIdx++, section);

    ElaText *hint = new ElaText("One template per line. Applied to the prompt bar in Chat page.", this);
    hint->setTextPixelSize(12);
    hint->setStyleSheet("color: rgba(128,128,128,0.7);");
    layout->insertWidget(insertIdx++, hint);

    QPlainTextEdit *templatesEdit = new QPlainTextEdit(this);
    templatesEdit->setObjectName("promptTemplatesEdit");
    templatesEdit->setMinimumHeight(100);
    templatesEdit->setMaximumHeight(150);
    templatesEdit->setPlainText(m_settings->promptTemplates().join('\n'));
    layout->insertWidget(insertIdx++, templatesEdit);

    connect(templatesEdit, &QPlainTextEdit::textChanged, this, [this, templatesEdit]() {
        QStringList lines;
        for (const QString &line : templatesEdit->toPlainText().split('\n')) {
            QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) lines.append(trimmed);
        }
        m_settings->setPromptTemplates(lines);
    });
}

void SettingGeneralPage::setupBackgroundSection()
{
    // 获取 .ui 中的内容布局，在 noteLabel 之前插入背景设置控件
    QVBoxLayout *layout = ui->verticalLayout_2;
    int insertIdx = layout->indexOf(ui->noteLabel);

    // --- Section Header ---
    ElaText *sectionBg = new ElaText("Background", this);
    QFont boldFont;
    boldFont.setBold(true);
    sectionBg->setFont(boldFont);
    layout->insertWidget(insertIdx++, sectionBg);

    // --- Paint Mode Area ---
    ElaScrollPageArea *paintModeArea = new ElaScrollPageArea(this);
    QHBoxLayout *paintModeLayout = new QHBoxLayout(paintModeArea);
    paintModeLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *paintModeLabel = new ElaText("Paint Mode", this);
    paintModeLabel->setTextPixelSize(15);
    paintModeLayout->addWidget(paintModeLabel);
    paintModeLayout->addStretch();

    ElaRadioButton *normalBtn = new ElaRadioButton("Normal", this);
    ElaRadioButton *pixmapBtn = new ElaRadioButton("Pixmap", this);
    ElaRadioButton *movieBtn = new ElaRadioButton("Movie", this);

    QButtonGroup *paintGroup = new QButtonGroup(this);
    paintGroup->addButton(normalBtn, 0);
    paintGroup->addButton(pixmapBtn, 1);
    paintGroup->addButton(movieBtn, 2);

    // 从配置恢复当前模式
    int currentMode = m_settings->backgroundPaintMode();
    if (auto *btn = paintGroup->button(currentMode))
        static_cast<ElaRadioButton*>(btn)->setChecked(true);

    paintModeLayout->addWidget(normalBtn);
    paintModeLayout->addWidget(pixmapBtn);
    paintModeLayout->addWidget(movieBtn);

    // 按钮切换 → 设置窗口绘制模式并持久化
    connect(paintGroup, QOverload<QAbstractButton*, bool>::of(&QButtonGroup::buttonToggled),
            this, [this, paintGroup](QAbstractButton *button, bool isToggled) {
        if (!isToggled) return;
        int mode = paintGroup->id(button);
        m_settings->setBackgroundPaintMode(mode);
        ElaWindow *window = dynamic_cast<ElaWindow*>(this->window());
        if (window) {
            window->setWindowPaintMode(static_cast<ElaWindowType::PaintMode>(mode));
        }
    });

    layout->insertWidget(insertIdx++, paintModeArea);

    // --- Pixmap Path Area ---
    ElaScrollPageArea *pixmapArea = new ElaScrollPageArea(this);
    QHBoxLayout *pixmapLayout = new QHBoxLayout(pixmapArea);
    pixmapLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *pixmapLabel = new ElaText("Background Image", this);
    pixmapLabel->setTextPixelSize(15);
    pixmapLayout->addWidget(pixmapLabel);
    pixmapLayout->addStretch();

    m_pixmapPathEdit = new ElaLineEdit(this);
    m_pixmapPathEdit->setPlaceholderText("Local path to .png/.jpg (empty = built-in)");
    m_pixmapPathEdit->setMinimumWidth(300);
    m_pixmapPathEdit->setText(m_settings->backgroundPixmapPath());
    pixmapLayout->addWidget(m_pixmapPathEdit);

    connect(m_pixmapPathEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setBackgroundPixmapPath(text);
        // 即时更新背景图
        ElaWindow *window = dynamic_cast<ElaWindow*>(this->window());
        if (window) {
            QPixmap pix(text);
            if (pix.isNull())
                pix = QPixmap(":/imgs/Miku.png");
            window->setWindowPixmap(ElaThemeType::Light, pix);
        }
    });

    layout->insertWidget(insertIdx++, pixmapArea);

    // --- Movie Path Area ---
    ElaScrollPageArea *movieArea = new ElaScrollPageArea(this);
    QHBoxLayout *movieLayout = new QHBoxLayout(movieArea);
    movieLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *movieLabel = new ElaText("Background Movie", this);
    movieLabel->setTextPixelSize(15);
    movieLayout->addWidget(movieLabel);
    movieLayout->addStretch();

    m_moviePathEdit = new ElaLineEdit(this);
    m_moviePathEdit->setPlaceholderText("Local path to .gif (empty = built-in)");
    m_moviePathEdit->setMinimumWidth(300);
    m_moviePathEdit->setText(m_settings->backgroundMoviePath());
    movieLayout->addWidget(m_moviePathEdit);

    connect(m_moviePathEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_settings->setBackgroundMoviePath(text);
        // 即时更新背景动画
        ElaWindow *window = dynamic_cast<ElaWindow*>(this->window());
        if (window) {
            QString path = text.isEmpty() ? ":/imgs/Miku.gif" : text;
            window->setWindowMoviePath(ElaThemeType::Light, path);
        }
    });

    layout->insertWidget(insertIdx++, movieArea);
}

void SettingGeneralPage::setupTTSSection()
{
    QVBoxLayout *layout = ui->verticalLayout_2;
    int insertIdx = layout->indexOf(ui->noteLabel);

    // --- Section Header ---
    ElaText *sectionTTS = new ElaText("TTS (Text-to-Speech)", this);
    QFont boldFont;
    boldFont.setBold(true);
    sectionTTS->setFont(boldFont);
    layout->insertWidget(insertIdx++, sectionTTS);

    // --- Enable TTS Area ---
    ElaScrollPageArea *enableArea = new ElaScrollPageArea(this);
    QHBoxLayout *enableLayout = new QHBoxLayout(enableArea);
    enableLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *enableLabel = new ElaText("Enable TTS", this);
    enableLabel->setTextPixelSize(15);
    enableLayout->addWidget(enableLabel);
    enableLayout->addStretch();

    m_ttsEnabledSwitch = new ElaToggleSwitch(this);
    m_ttsEnabledSwitch->setIsToggled(m_settings->ttsEnabled());
    enableLayout->addWidget(m_ttsEnabledSwitch);

    connect(m_ttsEnabledSwitch, &ElaToggleSwitch::toggled, this, [this]() {
        m_settings->setTtsEnabled(m_ttsEnabledSwitch->getIsToggled());
    });

    layout->insertWidget(insertIdx++, enableArea);

    // --- Voice Selection Area ---
    ElaScrollPageArea *voiceArea = new ElaScrollPageArea(this);
    QHBoxLayout *voiceLayout = new QHBoxLayout(voiceArea);
    voiceLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *voiceLabel = new ElaText("Voice", this);
    voiceLabel->setTextPixelSize(15);
    voiceLayout->addWidget(voiceLabel);
    voiceLayout->addStretch();

    m_ttsVoiceCombo = new ElaComboBox(this);
    m_ttsVoiceCombo->setMinimumWidth(250);

    // 常用中文和英文语音列表
    QStringList voices = {
        "zh-CN-XiaoxiaoNeural",
        "zh-CN-YunxiNeural",
        "zh-CN-XiaoyiNeural",
        "zh-CN-YunjianNeural",
        "zh-CN-YunyangNeural",
        "zh-CN-XiaochenNeural",
        "zh-CN-XiaohanNeural",
        "zh-CN-XiaomengNeural",
        "zh-CN-XiaomoNeural",
        "zh-CN-XiaoruiNeural",
        "zh-CN-XiaoshuangNeural",
        "zh-CN-XiaoxuanNeural",
        "zh-CN-XiaoyanNeural",
        "zh-CN-XiaozhenNeural",
        "zh-CN-YunfengNeural",
        "zh-CN-YunhaoNeural",
        "zh-CN-YunzeNeural",
        "en-US-JennyNeural",
        "en-US-GuyNeural",
        "en-US-AriaNeural",
        "ja-JP-NanamiNeural",
        "ja-JP-KeitaNeural",
    };
    m_ttsVoiceCombo->addItems(voices);

    // 恢复保存的语音选择
    QString savedVoice = m_settings->ttsVoice();
    int voiceIdx = voices.indexOf(savedVoice);
    if (voiceIdx >= 0)
        m_ttsVoiceCombo->setCurrentIndex(voiceIdx);

    connect(m_ttsVoiceCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_settings->setTtsVoice(text);
    });

    voiceLayout->addWidget(m_ttsVoiceCombo);
    layout->insertWidget(insertIdx++, voiceArea);
}

void SettingGeneralPage::loadSettings()
{
    ui->plainTextEdit_systemPrompt->setPlainText(m_settings->systemPrompt());
    ui->doubleSpinBox_temperature->setValue(m_settings->temperature());
    ui->spinBox_maxTokens->setValue(m_settings->maxTokens());
}

void SettingGeneralPage::connectAutoSave()
{
    connect(ui->plainTextEdit_systemPrompt, &QPlainTextEdit::textChanged, this, [this]() {
        m_settings->setSystemPrompt(ui->plainTextEdit_systemPrompt->toPlainText());
        emit settingsChanged();
    });
    connect(ui->doubleSpinBox_temperature, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
        m_settings->setTemperature(val);
        emit settingsChanged();
    });
    connect(ui->spinBox_maxTokens, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) {
        m_settings->setMaxTokens(val);
        emit settingsChanged();
    });
}
