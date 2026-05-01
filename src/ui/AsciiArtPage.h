#pragma once
#include <QWidget>
#include <QImage>

class QLabel;
class QTextBrowser;
class QSpinBox;
class ElaPushButton;
class ElaComboBox;
class ElaToggleSwitch;

/**
 * @brief ASCII Art 工具页 —— 将图片转换为 ASCII 字符画
 *
 * 功能：
 * - 选择图片（按钮 / 拖拽）后实时预览 ASCII 效果
 * - 可调参数：宽度（字符数）、字符集（简单/详细）、彩色开关
 * - 输出操作：复制到剪贴板 / 保存为 TXT / 保存为 HTML（彩色）
 */
class AsciiArtPage : public QWidget
{
    Q_OBJECT
public:
    explicit AsciiArtPage(QWidget *parent = nullptr);

    /**
     * @brief 将图片转换为灰度 ASCII 字符串（静态方法，供聊天页调用）
     * @param img      原始图片
     * @param width    目标宽度（字符数）
     * @param detailed true 使用详细字符集，false 使用简单字符集
     * @return 多行 ASCII 字符串
     */
    static QString convertToAscii(const QImage &img, int width, bool detailed);

    /**
     * @brief 将图片转换为彩色 HTML ASCII 字符画
     * @param img      原始图片
     * @param width    目标宽度（字符数）
     * @param detailed true 使用详细字符集，false 使用简单字符集
     * @return HTML 片段（每个字符用 <span style="color:..."> 包裹）
     */
    static QString convertToColorHtml(const QImage &img, int width, bool detailed);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupUI();
    void loadImage(const QString &filePath);
    void updatePreview();

    // 控件
    QLabel *m_imagePreview;             // 原图缩略图
    QTextBrowser *m_asciiPreview;       // ASCII 预览区
    ElaPushButton *m_selectBtn;         // 选择图片按钮
    QSpinBox *m_widthSpin;              // 宽度调节
    ElaComboBox *m_charsetCombo;        // 字符集选择
    ElaToggleSwitch *m_colorSwitch;     // 彩色开关
    ElaPushButton *m_copyBtn;           // 复制
    ElaPushButton *m_saveTxtBtn;        // 保存 TXT
    ElaPushButton *m_saveHtmlBtn;       // 保存 HTML
    ElaPushButton *m_savePngBtn;        // 保存 PNG 图片

    QImage m_sourceImage;               // 当前加载的原始图片
    QString m_currentAscii;             // 当前生成的 ASCII 文本
    QString m_currentHtml;              // 当前生成的彩色 HTML
};
