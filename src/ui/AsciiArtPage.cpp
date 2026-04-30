#include "AsciiArtPage.h"
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFile>
#include <QTextStream>
#include <QSpinBox>
#include <QDesktopServices>
#include <QUrl>
#include <ElaPushButton.h>
#include <ElaComboBox.h>
#include <ElaText.h>

// ASCII 字符集（从暗到亮排列）
static const char *SIMPLE_CHARSET  = " .:-=+*#%@";
static const char *DETAILED_CHARSET = " .'`^\",:;Il!i><~+_-?][}{1)(|/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

// ============================================================================
// 构造函数
// ============================================================================

AsciiArtPage::AsciiArtPage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setAcceptDrops(true);
}

// ============================================================================
// UI 构建
// ============================================================================

void AsciiArtPage::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // --- 标题 ---
    ElaText *title = new ElaText("ASCII Art", this);
    QFont titleFont;
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    mainLayout->addWidget(title);

    // --- 上半部分：原图预览 + ASCII 预览 ---
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // 左侧：原图缩略图
    QWidget *leftPanel = new QWidget(splitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_imagePreview = new QLabel(leftPanel);
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setMinimumSize(200, 200);
    m_imagePreview->setObjectName("asciiImagePreview");
    m_imagePreview->setStyleSheet(
        "QLabel { background: rgba(128,128,128,0.06); border: 2px dashed rgba(128,128,128,0.2); "
        "border-radius: 8px; }");
    m_imagePreview->setText("Drag image here\nor click Select Image");
    leftLayout->addWidget(m_imagePreview);

    // 右侧：ASCII 预览
    m_asciiPreview = new QTextBrowser(splitter);
    m_asciiPreview->setReadOnly(true);
    m_asciiPreview->setFrameShape(QFrame::NoFrame);
    m_asciiPreview->setObjectName("asciiTextPreview");
    m_asciiPreview->setLineWrapMode(QTextBrowser::NoWrap);
    m_asciiPreview->setStyleSheet(
        "QTextBrowser { background: rgba(0,0,0,0.85); color: #00ff00; "
        "font-family: Consolas, 'Courier New', monospace; font-size: 8px; "
        "border-radius: 8px; padding: 8px; }");

    splitter->addWidget(leftPanel);
    splitter->addWidget(m_asciiPreview);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter, 1);

    // --- 下半部分：控制栏 ---
    QWidget *controlBar = new QWidget(this);
    QHBoxLayout *controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(12);

    // 选择图片按钮
    m_selectBtn = new ElaPushButton("Select Image", controlBar);
    m_selectBtn->setFixedHeight(36);
    controlLayout->addWidget(m_selectBtn);

    // 宽度调节
    ElaText *widthLabel = new ElaText("Width:", controlBar);
    widthLabel->setTextPixelSize(14);
    controlLayout->addWidget(widthLabel);

    m_widthSpin = new QSpinBox(controlBar);
    m_widthSpin->setRange(20, 200);
    m_widthSpin->setValue(80);
    m_widthSpin->setSingleStep(10);
    controlLayout->addWidget(m_widthSpin);

    // 字符集选择
    ElaText *charsetLabel = new ElaText("Charset:", controlBar);
    charsetLabel->setTextPixelSize(14);
    controlLayout->addWidget(charsetLabel);

    m_charsetCombo = new ElaComboBox(controlBar);
    m_charsetCombo->addItem("Simple");
    m_charsetCombo->addItem("Detailed");
    controlLayout->addWidget(m_charsetCombo);

    controlLayout->addStretch();
    mainLayout->addWidget(controlBar);

    // --- 操作按钮栏 ---
    QWidget *actionBar = new QWidget(this);
    QHBoxLayout *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);

    m_copyBtn = new ElaPushButton("Copy", actionBar);
    m_copyBtn->setFixedHeight(36);
    m_copyBtn->setEnabled(false);
    actionLayout->addWidget(m_copyBtn);

    m_saveTxtBtn = new ElaPushButton("Save TXT", actionBar);
    m_saveTxtBtn->setFixedHeight(36);
    m_saveTxtBtn->setEnabled(false);
    actionLayout->addWidget(m_saveTxtBtn);

    actionLayout->addStretch();
    mainLayout->addWidget(actionBar);

    // ===================== 信号连接 =====================

    // 选择图片
    connect(m_selectBtn, &ElaPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(
            this, "Select Image", QString(),
            "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)");
        if (!file.isEmpty()) loadImage(file);
    });

    // 参数变化 → 实时更新预览
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { updatePreview(); });
    connect(m_charsetCombo, &ElaComboBox::currentTextChanged, this, [this]() { updatePreview(); });

    // 复制到剪贴板
    connect(m_copyBtn, &ElaPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_currentAscii);
    });

    // 保存 TXT（保存后自动用系统默认程序打开）
    connect(m_saveTxtBtn, &ElaPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Save ASCII Art", "ascii_art.txt", "Text (*.txt)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out.setCodec("UTF-8");
            out << m_currentAscii;
            f.close();
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
}

// ============================================================================
// 图片加载
// ============================================================================

/** @brief 加载图片并刷新预览 */
void AsciiArtPage::loadImage(const QString &filePath)
{
    QImage img(filePath);
    if (img.isNull()) return;

    m_sourceImage = img;

    // 显示缩略图
    QPixmap pix = QPixmap::fromImage(img);
    m_imagePreview->setPixmap(pix.scaled(
        m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 启用操作按钮
    m_copyBtn->setEnabled(true);
    m_saveTxtBtn->setEnabled(true);

    updatePreview();
}

// ============================================================================
// 拖拽事件
// ============================================================================

void AsciiArtPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AsciiArtPage::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            loadImage(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}

// ============================================================================
// 预览更新
// ============================================================================

/** @brief 根据当前参数重新生成 ASCII 并刷新预览 */
void AsciiArtPage::updatePreview()
{
    if (m_sourceImage.isNull()) return;

    int width = m_widthSpin->value();
    bool detailed = (m_charsetCombo->currentIndex() == 1);

    m_currentAscii = convertToAscii(m_sourceImage, width, detailed);
    m_asciiPreview->setPlainText(m_currentAscii);
}

// ============================================================================
// 核心转换算法
// ============================================================================

/**
 * @brief 将图片转换为灰度 ASCII 字符串
 *
 * 算法：
 * 1. 按目标宽度等比缩放图片（高度乘 0.5 补偿字符宽高比）
 * 2. 转为灰度图
 * 3. 逐像素将灰度值映射到字符集中的字符
 */
QString AsciiArtPage::convertToAscii(const QImage &img, int width, bool detailed)
{
    if (img.isNull() || width <= 0) return {};

    const char *charset = detailed ? DETAILED_CHARSET : SIMPLE_CHARSET;
    int charsetLen = static_cast<int>(strlen(charset));

    // 等比缩放（字符高度约为宽度的 2 倍，所以高度除 2）
    int height = img.height() * width / img.width() / 2;
    if (height <= 0) height = 1;

    QImage scaled = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);

    QString result;
    result.reserve((width + 1) * height);

    for (int y = 0; y < scaled.height(); ++y) {
        const uchar *line = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            int gray = line[x];  // 0(黑) ~ 255(白)
            int idx = gray * (charsetLen - 1) / 255;
            result += QChar(charset[idx]);
        }
        result += '\n';
    }
    return result;
}
