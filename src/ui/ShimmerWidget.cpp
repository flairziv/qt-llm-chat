#include "ShimmerWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QPalette>
#include <QLinearGradient>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QHideEvent>
#include <QtMath>

namespace {
constexpr int kDefaultSide = 200;       // 占位符默认边长
constexpr int kAnimDurationMs = 1400;   // 一次扫光时长
constexpr qreal kAngleDegrees = 20.0;   // 扫光倾斜角度
constexpr qreal kGradientRatio = 0.5;   // 扫光宽度占对角线的比例
}

ShimmerWidget::ShimmerWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setMinimumSize(kDefaultSide, kDefaultSide);

    // 无限循环动画：m_progress 从 0 到 1 + ratio（保证扫光完全离开右侧后再循环）
    m_animation = new QPropertyAnimation(this, "shimmerProgress", this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0 + kGradientRatio);
    m_animation->setDuration(kAnimDurationMs);
    m_animation->setLoopCount(-1);
}

QSize ShimmerWidget::sizeHint() const
{
    return QSize(kDefaultSide, kDefaultSide);
}

void ShimmerWidget::setShimmerProgress(qreal p)
{
    m_progress = p;
    update();
}

void ShimmerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_animation->state() != QAbstractAnimation::Running) {
        m_animation->start();
    }
}

void ShimmerWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_animation->stop();
}

void ShimmerWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = 8.0;

    // ---- 1. 底层灰色背景（半透明，跟随主题色）----
    const QColor base = palette().color(QPalette::Mid);
    QColor bg = base;
    bg.setAlpha(60);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(rect, radius, radius);

    // ---- 2. 中心图标（图片占位符提示）----
    // 简单画一个相机/山形图标，纯几何图形避免引入资源依赖
    {
        painter.save();
        QColor iconColor = palette().color(QPalette::Text);
        iconColor.setAlpha(70);
        painter.setPen(QPen(iconColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        const qreal cx = rect.center().x();
        const qreal cy = rect.center().y();
        const qreal w = qMin(rect.width(), rect.height()) * 0.32;
        const qreal h = w * 0.7;

        // 矩形外框
        QRectF iconRect(cx - w / 2, cy - h / 2, w, h);
        painter.drawRoundedRect(iconRect, 4, 4);

        // 山形（两个三角）
        QPainterPath path;
        const qreal baseY = iconRect.bottom() - 2;
        path.moveTo(iconRect.left() + 4, baseY);
        path.lineTo(iconRect.left() + w * 0.35, baseY - h * 0.45);
        path.lineTo(iconRect.left() + w * 0.55, baseY - h * 0.2);
        path.lineTo(iconRect.left() + w * 0.75, baseY - h * 0.6);
        path.lineTo(iconRect.right() - 4, baseY);
        path.closeSubpath();
        painter.fillPath(path, iconColor);

        // 太阳/小圆点
        QColor dotColor = iconColor;
        dotColor.setAlpha(110);
        painter.setBrush(dotColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(iconRect.right() - w * 0.22, iconRect.top() + h * 0.3),
                            w * 0.07, w * 0.07);
        painter.restore();
    }

    // ---- 3. 扫光层（线性渐变）----
    const qreal width = rect.width();
    const qreal height = rect.height();
    const qreal diagonal = qSqrt(width * width + height * height);
    const qreal gradientWidth = diagonal * kGradientRatio;
    const qreal totalDistance = diagonal + gradientWidth;
    const qreal currentOffset = m_progress * totalDistance - gradientWidth;

    const qreal angleRad = qDegreesToRadians(kAngleDegrees);
    const qreal cosA = qCos(angleRad);
    const qreal sinA = qSin(angleRad);

    QPointF start(currentOffset * cosA, currentOffset * sinA);
    QPointF end((currentOffset + gradientWidth) * cosA, (currentOffset + gradientWidth) * sinA);

    QColor highlight = palette().color(QPalette::BrightText);
    highlight.setAlpha(70);

    QLinearGradient gradient(start, end);
    gradient.setColorAt(0.0, QColor(highlight.red(), highlight.green(), highlight.blue(), 0));
    gradient.setColorAt(0.5, highlight);
    gradient.setColorAt(1.0, QColor(highlight.red(), highlight.green(), highlight.blue(), 0));

    // 在圆角矩形区域内画扫光，用 clip 限制范围
    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clipPath);
    painter.fillRect(rect, gradient);
    painter.restore();
}
