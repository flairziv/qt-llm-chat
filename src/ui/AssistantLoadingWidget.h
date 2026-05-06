#pragma once
#include <QWidget>
#include <QString>

class QSvgRenderer;
class QLabel;
class QHBoxLayout;
class QPropertyAnimation;
class QSequentialAnimationGroup;
class QParallelAnimationGroup;

/**
 * @brief AI 助手加载指示器 —— 一只眨眼/扫视的小图标 + 状态文本
 *
 * 构成：
 *   [ SVG 头像 (40x40) ] [ 状态文本 ]
 *
 * 头像本身是单张静态 SVG（resources/imgs/ai_assistant.svg），里面分两组：
 *   <g id="body"> — 脸部、耳朵、鼻子、嘴巴（永远不动）
 *   <g id="eyes"> — 两只眼睛（围绕 pivot 做 scale/translate 动画）
 *
 * 动画分三轨，并行循环播放（3000ms 一周期）：
 *   - eyeScaleY  : 1.0 ↔ 0.1，模拟眨眼，每周期 4 次
 *   - eyeTransX  : 0 → +12 → -12 → 0，左顾右盼
 *   - eyeTransY  : 0 → -8 → 0，最后阶段抬头
 *
 * 这套思路移植自 rikkahub 的 RabbitLoadingIndicator，但兔子素材是用户自备的，
 * 不与原项目共享 license。Qt 端用 QPropertyAnimation 在代码侧驱动，不依赖
 * AnimatedVectorDrawable。
 *
 * 接口：
 *   start()         — 开始动画并显示头像
 *   stop()          — 停止动画并隐藏头像（文本仍然保留）
 *   setText(s)      — 设置右侧状态文字；空串则隐藏 label
 *   isAnimating()
 *
 * 默认情况下整个 widget 在 ChatPage 状态栏位置常驻，靠 setText("") 让它"看起来不存在"。
 */
class AssistantLoadingWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal eyeScaleY  READ eyeScaleY  WRITE setEyeScaleY)
    Q_PROPERTY(qreal eyeTransX  READ eyeTransX  WRITE setEyeTransX)
    Q_PROPERTY(qreal eyeTransY  READ eyeTransY  WRITE setEyeTransY)
public:
    explicit AssistantLoadingWidget(QWidget *parent = nullptr);

    void start();
    void stop();
    void setText(const QString &text);
    QString text() const;
    bool isAnimating() const;

    qreal eyeScaleY() const { return m_eyeScaleY; }
    qreal eyeTransX() const { return m_eyeTransX; }
    qreal eyeTransY() const { return m_eyeTransY; }
    void setEyeScaleY(qreal v);
    void setEyeTransX(qreal v);
    void setEyeTransY(qreal v);

private slots:
    /**
     * @brief 按当前主题（light/dark）重新生成 SVG 字节并加载到 renderer
     *
     * 思路：构造时把原始 svg 字节缓存到 m_svgTemplate，主题切换时根据
     * eTheme->getThemeMode() 决定是否把 #333333 替换为浅色，再 reload。
     * 替换字符串而不是修改 path 颜色，因为 QSvgRenderer 不支持运行时改 fill。
     */
    void reloadSvgForTheme();

protected:
    /** @brief 自绘头像区域：先渲染 body，再变换坐标后渲染 eyes */
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void buildAnimations();
    QRectF iconRect() const;       // 头像在 widget 内的目标矩形

    QSvgRenderer *m_renderer = nullptr;
    QByteArray m_svgTemplate;     // 原始 svg 字节缓存，主题切换时基于此生成新版本
    QLabel *m_textLabel = nullptr;
    QHBoxLayout *m_layout = nullptr;

    QSequentialAnimationGroup *m_blinkGroup = nullptr;       // 4 次眨眼依次执行
    QPropertyAnimation *m_horizAnim = nullptr;               // 左顾右盼
    QPropertyAnimation *m_vertAnim = nullptr;                // 抬头
    QParallelAnimationGroup *m_rootGroup = nullptr;          // 三轨并行 + 无限循环

    qreal m_eyeScaleY = 1.0;
    qreal m_eyeTransX = 0.0;
    qreal m_eyeTransY = 0.0;

    static constexpr int kIconSide = 40;     // 头像尺寸
    static constexpr qreal kPivotX = 128.0;  // 来自 svg 注释 eyes-pivot
    static constexpr qreal kPivotY = 115.0;
};
