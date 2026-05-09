#include "ImageViewerDialog.h"

#include <QApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

ImageViewerDialog::ImageViewerDialog(const QPixmap &original, QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint)
{
    // 关闭即销毁，调用方只 show() 不持有引用
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    // 透明黑底，让图片像被托起来一样浮在桌面上
    setStyleSheet("background-color: rgba(0, 0, 0, 230);");

    m_scene = new QGraphicsScene(this);
    m_item = m_scene->addPixmap(original);
    m_scene->setSceneRect(m_item->boundingRect());

    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setStyleSheet("background: transparent;");
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 滚轮缩放以鼠标位置为锚：放大时不会跑到中心去
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_view->viewport()->installEventFilter(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    // 初始尺寸：占可用屏幕的 80%，居中
    QScreen *screen = parent ? parent->screen() : QApplication::primaryScreen();
    if (!screen) screen = QApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        resize(int(avail.width() * 0.8), int(avail.height() * 0.8));
        move(avail.center().x() - width() / 2,
             avail.center().y() - height() / 2);
    }
}

void ImageViewerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 在 showEvent 里 fitInView 而不是构造函数：构造时 view 还没拿到最终尺寸，
    // 适配会算错。每次窗口首次显示后 fit 一次即可，后续缩放/拖动都尊重用户操作。
    if (!m_initialFitDone && m_item) {
        m_view->fitInView(m_item, Qt::KeepAspectRatio);
        m_initialFitDone = true;
    }
}

void ImageViewerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool ImageViewerDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view->viewport()) {
        if (event->type() == QEvent::Wheel) {
            // 滚轮：上滚放大、下滚缩小。AnchorUnderMouse 已经处理了锚点。
            QWheelEvent *we = static_cast<QWheelEvent*>(event);
            const qreal step = 1.15;
            const qreal factor = we->angleDelta().y() > 0 ? step : 1.0 / step;
            m_view->scale(factor, factor);
            return true;  // 吃掉事件，避免 view 默认行为去滚动条
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_item) {
                // 双击恢复初始适配
                m_view->resetTransform();
                m_view->fitInView(m_item, Qt::KeepAspectRatio);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}
