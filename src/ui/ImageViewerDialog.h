#pragma once
#include <QDialog>
#include <QPixmap>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;

/**
 * @brief 全屏图片查看器（frameless QDialog 包裹 QGraphicsView）
 *
 * 行为：
 *   - 启动时按"长边贴满 80% 屏幕、保持比例"显示
 *   - 鼠标滚轮：以光标位置为锚点缩放（×1.15 / ÷1.15）
 *   - 按住左键拖动：平移（依赖 QGraphicsView 的 ScrollHandDrag）
 *   - 双击：恢复 fitInView 适配
 *   - Esc：关闭
 *
 * 使用 QGraphicsView 而不是手撸 paintEvent，因为它内置了：
 *   - 平移（dragMode = ScrollHandDrag）
 *   - 锚点缩放（transformationAnchor = AnchorUnderMouse + scale()）
 *   - 平滑变换（QPainter::SmoothPixmapTransform）
 *
 * 调用方负责把 *已解码* 的原图（QPixmap）传进来；不持有 fileData，
 * 不重复解码。MessageBubble 的 m_originalPixmapCache 已经缓存了原图，
 * 直接复用即可。
 */
class ImageViewerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImageViewerDialog(const QPixmap &original, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_item = nullptr;
    bool m_initialFitDone = false;
};
