#pragma once
#include <QWidget>

class QPropertyAnimation;

/**
 * @brief 图片生成中的占位符 widget —— 带 shimmer（扫光）动画的灰色矩形
 *
 * 用法：
 *   ShimmerWidget *placeholder = new ShimmerWidget(parent);
 *   placeholder->setFixedSize(200, 200);
 *   layout->addWidget(placeholder);
 *
 * 设计要点：
 * - 通过 Q_PROPERTY 暴露 shimmerProgress 给 QPropertyAnimation 驱动 [0, 1+gradWidth]
 * - 可见时启动无限循环动画，隐藏时停止（避免后台空跑）
 * - 渐变方向 20° 倾斜，扫光宽度约为对角线 50%
 * - 中央绘制图标占位，提示用户"图片正在加载"
 *
 * 灵感来源：rikkahub 的 Shimmer Modifier（基于 Compose drawWithContent + DstIn 混合）
 *           Qt 端用 QLinearGradient + drawRect 等价实现
 */
class ShimmerWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal shimmerProgress READ shimmerProgress WRITE setShimmerProgress)
public:
    explicit ShimmerWidget(QWidget *parent = nullptr);

    qreal shimmerProgress() const { return m_progress; }
    void setShimmerProgress(qreal p);

    /** @brief 设置占位符尺寸（建议 200x200，匹配生成图片缩放后的常见大小） */
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    qreal m_progress = 0.0;
    QPropertyAnimation *m_animation;
};
