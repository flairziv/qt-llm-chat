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

SettingGeneralPage::SettingGeneralPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingGeneralPage)
    , m_settings(settings)
{
    ui->setupUi(this);
    ui->scrollArea->viewport()->setAutoFillBackground(false);
    setupBackgroundSection();
    loadSettings();
    connectAutoSave();
}

SettingGeneralPage::~SettingGeneralPage()
{
    delete ui;
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
