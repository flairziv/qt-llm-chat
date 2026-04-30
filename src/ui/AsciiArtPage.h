#pragma once
#include <QWidget>
#include <QImage>

class QLabel;
class QTextBrowser;
class QSpinBox;
class ElaPushButton;
class ElaComboBox;

/**
 * @brief ASCII Art 工具页 —— 将图片转换为 ASCII 字符画
 *
 * 基础版（灰度）：
 * - 选择图片（按钮 / 拖拽）后实时预览 ASCII 效果
 * - 可调参数：宽度（字符数）、字符集（简单/详细）
 * - 输出操作：复制到剪贴板 / 保存为 TXT
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
    ElaPushButton *m_copyBtn;           // 复制
    ElaPushButton *m_saveTxtBtn;        // 保存 TXT

    QImage m_sourceImage;               // 当前加载的原始图片
    QString m_currentAscii;             // 当前生成的 ASCII 文本
};
