#include "AssistantLoadingWidget.h"

#include <ElaTheme.h>
#include <QFile>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QSvgRenderer>

namespace {

constexpr int kCycleMs = 3000;          // 一个动画循环周期
constexpr int kBlinkDurMs = 100;        // 单次眨眼闭合到睁开的总耗时
constexpr int kSaccadeMs = 250;         // 眼球横向扫视过渡
constexpr int kLookUpMs = 250;          // 抬头过渡
constexpr qreal kSaccadeAmount = 12.0;  // 横向偏移量（svg viewBox 单位）
constexpr qreal kLookUpAmount = 8.0;    // 抬头偏移量

/**
 * @brief 构造一段眨眼动画（scaleY 从 1 → 0.1 → 1）
 *
 * 用 QSequentialAnimationGroup 串联两个反向 QPropertyAnimation 实现，避免使用
 * keyValues（旧版 Qt 的 setKeyValueAt 在某些场景下时间精度不准）。
 */
QSequentialAnimationGroup* makeBlink(QObject *target)
{
    auto *seq = new QSequentialAnimationGroup(target);

    auto *close = new QPropertyAnimation(target, "eyeScaleY", seq);
    close->setDuration(kBlinkDurMs / 2);
    close->setStartValue(1.0);
    close->setEndValue(0.1);

    auto *open = new QPropertyAnimation(target, "eyeScaleY", seq);
    open->setDuration(kBlinkDurMs / 2);
    open->setStartValue(0.1);
    open->setEndValue(1.0);

    seq->addAnimation(close);
    seq->addAnimation(open);
    return seq;
}

/** @brief 构造一段「保持当前值」的占位动画（用 PauseAnimation 等价物） */
QPropertyAnimation* makeHold(QObject *target, const char *prop, qreal value, int durationMs)
{
    auto *anim = new QPropertyAnimation(target, prop, target);
    anim->setDuration(durationMs);
    anim->setStartValue(value);
    anim->setEndValue(value);
    return anim;
}

QPropertyAnimation* makeTransition(QObject *target, const char *prop,
                                   qreal from, qreal to, int durationMs)
{
    auto *anim = new QPropertyAnimation(target, prop, target);
    anim->setDuration(durationMs);
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setEasingCurve(QEasingCurve::OutQuad);
    return anim;
}

} // namespace

AssistantLoadingWidget::AssistantLoadingWidget(QWidget *parent)
    : QWidget(parent)
{
    // 缓存原始 svg 字节，主题切换时基于此重新生成
    QFile f(QStringLiteral(":/imgs/ai_assistant.svg"));
    if (f.open(QIODevice::ReadOnly)) {
        m_svgTemplate = f.readAll();
        f.close();
    }

    m_renderer = new QSvgRenderer(this);
    reloadSvgForTheme();  // 按当前主题首次加载

    // 监听 ElaTheme 切换信号，主题变更时重新生成 svg
    connect(eTheme, &ElaTheme::themeModeChanged, this,
            &AssistantLoadingWidget::reloadSvgForTheme);

    m_textLabel = new QLabel(this);
    m_textLabel->setObjectName("assistantStatusText");
    QFont f2 = m_textLabel->font();
    f2.setPointSize(qMax(9, f2.pointSize()));
    m_textLabel->setFont(f2);
    // 文字色由 reloadSvgForTheme 根据当前主题设置（避免硬编码灰色在 dark 主题下偏暗）

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(8);
    m_layout->addSpacing(kIconSide);  // 头像绘制区域占位（由 paintEvent 自绘）
    m_layout->addWidget(m_textLabel, 1);

    setMinimumHeight(kIconSide);

    buildAnimations();
    reloadSvgForTheme();  // 再次调用确保 m_textLabel 颜色被设置（构造前段调用时 textLabel 还没建好）
}

void AssistantLoadingWidget::reloadSvgForTheme()
{
    if (m_svgTemplate.isEmpty() || !m_renderer) {
        return;
    }

    const bool isDark = (eTheme->getThemeMode() == ElaThemeType::Dark);

    // ---- 1. SVG 颜色调整 ----
    // light 主题：保持 svg 原色（#333333 深灰描线 + 白脸 + 粉鼻）
    // dark 主题：把深色描线和眼睛换成浅色，避免在暗背景上"消失"
    QByteArray adjusted = m_svgTemplate;
    if (isDark) {
        adjusted.replace("#333333", "#D8D8D8");
    }
    m_renderer->load(adjusted);

    // ---- 2. 状态文字色跟随主题 ----
    if (m_textLabel) {
        const QString textColor = isDark ? "#a8a8a8" : "#6e6e6e";
        m_textLabel->setStyleSheet(QStringLiteral("color: %1;").arg(textColor));
    }

    update();
}

QRectF AssistantLoadingWidget::iconRect() const
{
    // 头像永远画在 widget 左侧 kIconSide × kIconSide 区域，垂直居中
    const qreal y = (height() - kIconSide) / 2.0;
    return QRectF(0, y, kIconSide, kIconSide);
}

void AssistantLoadingWidget::buildAnimations()
{
    // ---- 1. 眨眼轨：4 次眨眼分别在 0% / 38% / 67% / 95% 触发 ----
    // 实现方式：sequential group = (hold 等待) + (blink) + (hold) + (blink) ...
    // 4 次眨眼的间隙时间分别为：0、1140、870、840（合计 ≈ 2850ms，剩 150ms 给 4 次眨眼本身）
    auto *blinkSeq = new QSequentialAnimationGroup(this);
    const QVector<int> blinkOffsetMs = {0, 1140, 870, 840};
    int accumulated = 0;
    for (int wait : blinkOffsetMs) {
        if (wait > 0) {
            blinkSeq->addAnimation(makeHold(this, "eyeScaleY", 1.0, wait));
            accumulated += wait;
        }
        blinkSeq->addAnimation(makeBlink(this));
        accumulated += kBlinkDurMs;
    }
    // 最后凑够 3000ms
    if (accumulated < kCycleMs) {
        blinkSeq->addAnimation(makeHold(this, "eyeScaleY", 1.0, kCycleMs - accumulated));
    }
    m_blinkGroup = blinkSeq;

    // ---- 2. 横向扫视轨：0 → +12（看右）→ -12（看左）→ 0 ----
    auto *horizSeq = new QSequentialAnimationGroup(this);
    horizSeq->addAnimation(makeHold(this, "eyeTransX", 0.0, 150));                          // 起始静止
    horizSeq->addAnimation(makeTransition(this, "eyeTransX", 0.0, kSaccadeAmount, kSaccadeMs));  // 看右
    horizSeq->addAnimation(makeHold(this, "eyeTransX", kSaccadeAmount, 540));               // 保持看右
    horizSeq->addAnimation(makeTransition(this, "eyeTransX", kSaccadeAmount, -kSaccadeAmount, kSaccadeMs));  // 看右 → 看左
    horizSeq->addAnimation(makeHold(this, "eyeTransX", -kSaccadeAmount, 600));              // 保持看左
    horizSeq->addAnimation(makeTransition(this, "eyeTransX", -kSaccadeAmount, 0.0, kSaccadeMs));  // 回中
    horizSeq->addAnimation(makeHold(this, "eyeTransX", 0.0, kCycleMs - (150 + kSaccadeMs + 540 + kSaccadeMs + 600 + kSaccadeMs)));  // 凑齐周期

    // ---- 3. 抬头轨：最后阶段往上 ----
    auto *vertSeq = new QSequentialAnimationGroup(this);
    vertSeq->addAnimation(makeHold(this, "eyeTransY", 0.0, 1990));                         // 前 ~66% 不动
    vertSeq->addAnimation(makeTransition(this, "eyeTransY", 0.0, -kLookUpAmount, kLookUpMs));  // 抬头
    vertSeq->addAnimation(makeHold(this, "eyeTransY", -kLookUpAmount, 510));               // 保持抬头
    vertSeq->addAnimation(makeTransition(this, "eyeTransY", -kLookUpAmount, 0.0, kLookUpMs));  // 回正

    // ---- 三轨并行 + 无限循环 ----
    m_rootGroup = new QParallelAnimationGroup(this);
    m_rootGroup->addAnimation(blinkSeq);
    m_rootGroup->addAnimation(horizSeq);
    m_rootGroup->addAnimation(vertSeq);
    m_rootGroup->setLoopCount(-1);

    m_horizAnim = nullptr;  // 不再单独保存（被 root 拥有）
    m_vertAnim = nullptr;
}

void AssistantLoadingWidget::start()
{
    if (m_rootGroup && m_rootGroup->state() != QAbstractAnimation::Running) {
        // 重置状态防止从中间帧开始
        m_eyeScaleY = 1.0;
        m_eyeTransX = 0.0;
        m_eyeTransY = 0.0;
        m_rootGroup->start();
    }
    update();
    show();  // 确保 widget 可见
}

void AssistantLoadingWidget::stop()
{
    if (m_rootGroup) {
        m_rootGroup->stop();
    }
    m_eyeScaleY = 1.0;
    m_eyeTransX = 0.0;
    m_eyeTransY = 0.0;
    update();
}

void AssistantLoadingWidget::setText(const QString &text)
{
    m_textLabel->setText(text);
    m_textLabel->setVisible(!text.isEmpty());
}

QString AssistantLoadingWidget::text() const
{
    return m_textLabel->text();
}

bool AssistantLoadingWidget::isAnimating() const
{
    return m_rootGroup && m_rootGroup->state() == QAbstractAnimation::Running;
}

void AssistantLoadingWidget::setEyeScaleY(qreal v)
{
    m_eyeScaleY = v;
    update();
}

void AssistantLoadingWidget::setEyeTransX(qreal v)
{
    m_eyeTransX = v;
    update();
}

void AssistantLoadingWidget::setEyeTransY(qreal v)
{
    m_eyeTransY = v;
    update();
}

void AssistantLoadingWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 隐藏时停止动画，避免后台空跑
    if (m_rootGroup) {
        m_rootGroup->pause();
    }
}

void AssistantLoadingWidget::paintEvent(QPaintEvent * /*event*/)
{
    if (!m_renderer || !m_renderer->isValid()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF target = iconRect();
    const QRectF vb = m_renderer->viewBoxF();
    if (vb.isEmpty()) {
        return;  // 防御：viewBox 不应为空
    }

    // QSvgRenderer::render(painter, id, bounds) 的关键行为：
    //   - bounds 非空 → 把 element 的 fastBounds 映射到 bounds（任意伸缩，可能畸变）
    //   - bounds 为空（QRectF()） → 把 element 映射到「整个 paint device」即 widget 本身
    //     这同样是任意伸缩（因为 widget 形状不一定匹配 svg viewBox）
    //
    // 解决：传 boundsOnElement(id) 自己作为 bounds，svg renderer 把 nodeBounds 映射到
    // 它本身 → identity 映射，element 按 svg 中的原始坐标渲染。
    // painter 端再统一做「svg 单位 → widget 像素」的等比缩放。
    const QRectF bodyBounds = m_renderer->boundsOnElement("body");
    const QRectF eyesBounds = m_renderer->boundsOnElement("eyes");

    // 把 painter 切到「target 内的 svg 坐标系」：body 和 eyes 共享同一个等比映射
    painter.save();
    painter.translate(target.left(), target.top());
    painter.scale(target.width() / vb.width(), target.height() / vb.height());
    painter.translate(-vb.left(), -vb.top());

    // ---- 1. 渲染身体：identity 映射，element 落在 svg 自然位置 ----
    m_renderer->render(&painter, "body", bodyBounds);

    // ---- 2. 渲染眼睛（带 pivot-based 动画变换）----
    painter.save();
    painter.translate(kPivotX, kPivotY);
    painter.translate(m_eyeTransX, m_eyeTransY);
    painter.scale(1.0, m_eyeScaleY);
    painter.translate(-kPivotX, -kPivotY);
    m_renderer->render(&painter, "eyes", eyesBounds);
    painter.restore();

    painter.restore();
}
