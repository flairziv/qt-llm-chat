#include "SettingToolsPage.h"
#include "core/AppSettings.h"
#include "core/ToolRegistry.h"
#include "core/Tool.h"

#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>

#include <ElaComboBox.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>

SettingToolsPage::SettingToolsPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
{
    setupUI();
}

void SettingToolsPage::setupUI()
{
    // 透明背景 + 无边框滚动区，跟随其它设置页外观（中央 stacked widget 已设透明）。
    setStyleSheet(QStringLiteral("SettingToolsPage { background: transparent; }"));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);

    QWidget *content = new QWidget(scroll);
    content->setAutoFillBackground(false);
    scroll->setWidget(content);

    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // --- 标题 + 说明 ---
    ElaText *title = new ElaText("Tool Settings", this);
    QFont titleFont;
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    ElaText *hint = new ElaText(
        "Control which tools the assistant may call and how each is gated. "
        "Risk level decides approval: Read-only runs silently, Mutating asks each time, "
        "Shell / Network asks and offers \"Allow for session\". Changes apply to the next message.",
        this);
    hint->setTextPixelSize(12);
    hint->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.8);"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // --- 总开关 ---
    ElaScrollPageArea *masterArea = new ElaScrollPageArea(this);
    QHBoxLayout *masterLayout = new QHBoxLayout(masterArea);
    masterLayout->setContentsMargins(15, 0, 15, 0);

    ElaText *masterLabel = new ElaText("Enable tools", this);
    masterLabel->setTextPixelSize(15);
    masterLayout->addWidget(masterLabel);
    masterLayout->addStretch();

    ElaToggleSwitch *masterSwitch = new ElaToggleSwitch(this);
    masterSwitch->setIsToggled(m_settings->toolsEnabled());
    masterLayout->addWidget(masterSwitch);
    layout->addWidget(masterArea);

    // --- 每个已注册工具一行 ---
    // 全部塞进 m_toolsContainer，总开关关闭时对容器 setEnabled(false) 让整组变灰。
    m_toolsContainer = new QWidget(this);
    QVBoxLayout *toolsLayout = new QVBoxLayout(m_toolsContainer);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(12);

    // 下拉项 index → AppSettings 存的 risk 字符串；index 0 = 空串（用注册默认）。
    const QStringList riskValues = { QString(), QStringLiteral("ReadOnly"),
                                     QStringLiteral("Mutating"), QStringLiteral("ShellOrNetwork") };

    const QList<Tool> tools = ToolRegistry::instance().availableTools();
    for (const Tool &tool : tools) {
        ElaScrollPageArea *row = new ElaScrollPageArea(m_toolsContainer);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(15, 0, 15, 0);
        rowLayout->setSpacing(10);

        ElaText *nameLabel = new ElaText(tool.name, m_toolsContainer);
        nameLabel->setTextPixelSize(15);
        if (!tool.description.isEmpty()) {
            nameLabel->setToolTip(tool.description);
        }
        rowLayout->addWidget(nameLabel);
        rowLayout->addStretch();

        // 风险下拉：Default(注册默认) / Read-only / Mutating / Shell or Network。
        ElaComboBox *riskCombo = new ElaComboBox(m_toolsContainer);
        riskCombo->setFixedWidth(180);
        riskCombo->addItem(QStringLiteral("Default (%1)").arg(riskLevelToString(tool.riskLevel)));
        riskCombo->addItem(QStringLiteral("Read-only"));
        riskCombo->addItem(QStringLiteral("Mutating"));
        riskCombo->addItem(QStringLiteral("Shell / Network"));
        int curIdx = riskValues.indexOf(m_settings->toolRiskOverride(tool.name));
        if (curIdx < 0) curIdx = 0;
        riskCombo->setCurrentIndex(curIdx);
        rowLayout->addWidget(riskCombo);

        // 启用开关
        ElaToggleSwitch *enableSwitch = new ElaToggleSwitch(m_toolsContainer);
        enableSwitch->setIsToggled(m_settings->toolEnabled(tool.name));
        rowLayout->addWidget(enableSwitch);

        // 初值都设好后再连接，避免构建期回写。
        const QString toolName = tool.name;
        connect(riskCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, toolName, riskValues](int idx) {
                    if (idx >= 0 && idx < riskValues.size()) {
                        m_settings->setToolRiskOverride(toolName, riskValues.at(idx));
                    }
                });
        connect(enableSwitch, &ElaToggleSwitch::toggled, this, [this, toolName, enableSwitch]() {
            m_settings->setToolEnabled(toolName, enableSwitch->getIsToggled());
        });

        toolsLayout->addWidget(row);
    }

    if (tools.isEmpty()) {
        ElaText *empty = new ElaText("No tools are registered.", m_toolsContainer);
        empty->setTextPixelSize(13);
        empty->setStyleSheet(QStringLiteral("color: rgba(128,128,128,0.8);"));
        toolsLayout->addWidget(empty);
    }

    layout->addWidget(m_toolsContainer);
    layout->addStretch(1);

    // 总开关联动：先按当前值设容器使能，再连接（避免构建期回写 setToolsEnabled）。
    m_toolsContainer->setEnabled(m_settings->toolsEnabled());
    connect(masterSwitch, &ElaToggleSwitch::toggled, this, [this, masterSwitch]() {
        const bool on = masterSwitch->getIsToggled();
        m_settings->setToolsEnabled(on);
        m_toolsContainer->setEnabled(on);
    });
}
