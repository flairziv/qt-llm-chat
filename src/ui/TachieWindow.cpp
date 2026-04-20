#include "TachieWindow.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QMenu>
#include <QDir>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 构造 / 析构
// ============================================================================

TachieWindow::TachieWindow(const QString &resourceDir, QWidget *parent)
    : QWidget(parent)
    , m_resourceDir(resourceDir)
{
    // 设置无边框、透明背景、始终置顶、工具窗口（不在任务栏显示）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);

    setupUI();
    loadConfig();

    // 加载默认表情图片
    if (!m_defaultEmotion.isEmpty()) {
        QPixmap pix = loadAndScaleImage(m_defaultEmotion);
        if (!pix.isNull()) {
            m_labelCurrent->setPixmap(pix);
            setFixedSize(pix.size());
            m_currentEmotion = m_defaultEmotion;
        }
    }

    // 初始位置：屏幕右下角
    resetPosition();
}

TachieWindow::~TachieWindow() = default;

// ============================================================================
// 界面初始化
// ============================================================================

/**
 * @brief 创建 3 个 QLabel 堆叠在 QGridLayout 同一格中
 *
 * 堆叠顺序（从下到上）：
 *   m_labelPrevious  -- 旧表情（淡出时显示）
 *   m_labelCurrent   -- 当前表情（淡入时显示）
 *   m_labelParticle  -- 粒子特效层（预留，鼠标事件穿透）
 */
void TachieWindow::setupUI()
{
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_labelPrevious = new QLabel(this);
    m_labelPrevious->setScaledContents(true);

    m_labelCurrent = new QLabel(this);
    m_labelCurrent->setScaledContents(true);

    m_labelParticle = new QLabel(this);
    m_labelParticle->setScaledContents(false);
    m_labelParticle->setAttribute(Qt::WA_TransparentForMouseEvents);  // 鼠标事件穿透

    // 三个 label 放在同一个格子中，形成层叠效果
    layout->addWidget(m_labelPrevious, 0, 0);
    layout->addWidget(m_labelCurrent, 0, 0);
    layout->addWidget(m_labelParticle, 0, 0);
}

// ============================================================================
// 配置加载
// ============================================================================

/**
 * @brief 加载配置文件和可用情绪列表
 *
 * 读取内容：
 * 1. config.ini  → tachie/size 缩放百分比
 * 2. anim.ini    → 每个情绪对应的动画类型（1-5）
 * 3. 扫描目录下所有 .png 文件名作为可用情绪列表
 */
void TachieWindow::loadConfig()
{
    // --- 读取 config.ini：缩放比例 ---
    QSettings config(m_resourceDir + "/config.ini", QSettings::IniFormat);
    config.setIniCodec("UTF-8");
    m_scalePercent = config.value("tachie/size", 100).toInt();
    m_defaultEmotion = QStringLiteral("正常");

    // --- 读取 anim.ini：情绪 → 动画类型映射 ---
    QSettings animConfig(m_resourceDir + "/anim.ini", QSettings::IniFormat);
    animConfig.setIniCodec("UTF-8");
    for (const QString &group : animConfig.childGroups()) {
        animConfig.beginGroup(group);
        m_animMap[group] = animConfig.value("animation", 0).toInt();
        animConfig.endGroup();
    }

    // --- 扫描目录下所有 PNG 文件，提取情绪名 ---
    QDir dir(m_resourceDir);
    QStringList pngFiles = dir.entryList({"*.png"}, QDir::Files);
    for (const QString &f : pngFiles) {
        m_emotionList.append(f.chopped(4));  // 去掉 ".png" 后缀
    }
}

// ============================================================================
// 图片加载与缩放
// ============================================================================

/**
 * @brief 加载指定情绪的 PNG 图片，并按 m_scalePercent 缩放
 * @param emotionName 情绪名（如 "高兴"、"生气"）
 * @return 缩放后的 QPixmap，加载失败则返回空 QPixmap
 */
QPixmap TachieWindow::loadAndScaleImage(const QString &emotionName)
{
    QString path = m_resourceDir + "/" + emotionName + ".png";
    QPixmap pixmap;
    if (!pixmap.load(path)) {
        qWarning() << "立绘图片未找到:" << path;
        // 回退到默认表情
        pixmap.load(m_resourceDir + "/" + m_defaultEmotion + ".png");
    }
    if (pixmap.isNull()) return {};

    // 按配置的百分比缩放
    if (m_scalePercent != 100) {
        pixmap = pixmap.scaled(
            pixmap.width() * m_scalePercent / 100,
            pixmap.height() * m_scalePercent / 100,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    }
    return pixmap;
}

// ============================================================================
// 表情切换（核心 API）
// ============================================================================

/**
 * @brief 切换到指定情绪的表情，播放淡入淡出过渡 + 对应动画
 *
 * 流程：
 * 1. 加载新表情图片
 * 2. 将当前图片复制到 m_labelPrevious（用于淡出）
 * 3. 将新图片设置到 m_labelCurrent（先隐藏，再淡入）
 * 4. 并行执行：m_labelPrevious 淡出 + m_labelCurrent 淡入
 * 5. 过渡完成后，根据 anim.ini 的映射播放对应的动画效果
 */
void TachieWindow::changeExpression(const QString &emotionName)
{
    if (emotionName == m_currentEmotion) {
        // 相同情绪仍播放动画，给用户视觉反馈（表示立绘在"回应"）
        int animType = m_animMap.value(emotionName, 0);
        if (animType > 0) {
            playAnimation(animType);
        }
        return;
    }

    QPixmap newPix = loadAndScaleImage(emotionName);
    if (newPix.isNull()) return;

    // 调整窗口大小以适配新图片
    setFixedSize(newPix.size());

    if (m_currentEmotion.isEmpty()) {
        // 首次加载，无需过渡动画
        m_labelCurrent->setPixmap(newPix);
    } else {
        playFadeTransition(newPix);
    }

    m_currentEmotion = emotionName;

    // 淡入淡出完成后（延迟 300ms），播放动画效果
    int animType = m_animMap.value(emotionName, 0);
    if (animType > 0) {
        QTimer::singleShot(300, this, [this, animType]() {
            playAnimation(animType);
        });
    }
}

// ============================================================================
// 淡入淡出过渡动画
// ============================================================================

/**
 * @brief 播放新旧表情之间的淡入淡出过渡
 *
 * 旧表情（m_labelPrevious）从不透明度 1.0 淡出到 0.0（500ms）
 * 新表情（m_labelCurrent）从不透明度 0.5 淡入到 1.0（200ms）
 * 两个动画并行执行，视觉上形成平滑的交叉溶解效果。
 */
void TachieWindow::playFadeTransition(const QPixmap &newPixmap)
{
    // 将当前图片复制到旧表情图层
    QPixmap currentPix = m_labelCurrent->pixmap(Qt::ReturnByValue);
    if (!currentPix.isNull()) {
        m_labelPrevious->setPixmap(currentPix);
    }
    m_labelCurrent->hide();
    m_labelCurrent->setPixmap(newPixmap);

    // 新表情图层：淡入（0.5 → 1.0，200ms）
    QGraphicsOpacityEffect *effectIn = new QGraphicsOpacityEffect(m_labelCurrent);
    m_labelCurrent->setGraphicsEffect(effectIn);
    QPropertyAnimation *animIn = new QPropertyAnimation(effectIn, "opacity");
    animIn->setDuration(200);
    animIn->setStartValue(0.5);
    animIn->setEndValue(1.0);

    // 旧表情图层：淡出（1.0 → 0.0，500ms）
    QGraphicsOpacityEffect *effectOut = new QGraphicsOpacityEffect(m_labelPrevious);
    m_labelPrevious->setGraphicsEffect(effectOut);
    QPropertyAnimation *animOut = new QPropertyAnimation(effectOut, "opacity");
    animOut->setDuration(500);
    animOut->setStartValue(1.0);
    animOut->setEndValue(0.0);

    // 并行启动两个动画
    animIn->start(QAbstractAnimation::DeleteWhenStopped);
    animOut->start(QAbstractAnimation::DeleteWhenStopped);
    m_labelCurrent->show();
}

// ============================================================================
// 动画系统（5 种类型，移植自原始 tachie.cpp）
// ============================================================================

/**
 * @brief 根据动画类型播放对应的动画效果
 *
 * 动画类型说明：
 *   1 - 上下抖动：向上移动 15px 再回到原位
 *   2 - 左右摇晃：向左 15px → 向右 15px → 回到原位
 *   3 - 放大：尺寸放大到 120%
 *   4 - 缩小：尺寸缩小（宽度减 50px，等比缩放）
 *   5 - 颤抖：快速上下左右微幅振动（4px，重复 10 次）
 */
void TachieWindow::playAnimation(int animationType)
{
    QSequentialAnimationGroup *group = new QSequentialAnimationGroup(this);

    switch (animationType) {
    case 1: {  // 上下抖动
        QPoint base = m_labelCurrent->pos();
        QPropertyAnimation *up = new QPropertyAnimation(m_labelCurrent, "pos");
        up->setDuration(250);
        up->setStartValue(base);
        up->setEndValue(base + QPoint(0, -15));

        QPropertyAnimation *down = new QPropertyAnimation(m_labelCurrent, "pos");
        down->setDuration(250);
        down->setStartValue(base + QPoint(0, -15));
        down->setEndValue(base);

        group->addAnimation(up);
        group->addAnimation(down);
        break;
    }
    case 2: {  // 左右摇晃
        QPoint base = m_labelCurrent->pos();
        QPropertyAnimation *left = new QPropertyAnimation(m_labelCurrent, "pos");
        left->setDuration(250);
        left->setStartValue(base);
        left->setEndValue(base + QPoint(-15, 0));

        QPropertyAnimation *right = new QPropertyAnimation(m_labelCurrent, "pos");
        right->setDuration(500);
        right->setStartValue(base + QPoint(-15, 0));
        right->setEndValue(base + QPoint(15, 0));

        QPropertyAnimation *center = new QPropertyAnimation(m_labelCurrent, "pos");
        center->setDuration(250);
        center->setStartValue(base + QPoint(15, 0));
        center->setEndValue(base);

        group->addAnimation(left);
        group->addAnimation(right);
        group->addAnimation(center);
        break;
    }
    case 3: {  // 放大（缩放到 120%）
        QPixmap pix = m_labelCurrent->pixmap(Qt::ReturnByValue);
        if (pix.isNull()) break;
        int w = m_labelCurrent->width();
        int h = m_labelCurrent->height();
        int endW = w * 1.2;
        int endH = h * 1.2;

        m_labelCurrent->setScaledContents(true);
        QPropertyAnimation *zoom = new QPropertyAnimation(m_labelCurrent, "size");
        zoom->setDuration(500);
        zoom->setStartValue(QSize(w, h));
        zoom->setEndValue(QSize(endW, endH));
        zoom->start(QAbstractAnimation::DeleteWhenStopped);
        return;  // 此动画不使用 group，单独启动
    }
    case 4: {  // 缩小（宽度减 50px，等比缩放，居中）
        QRect orig = m_labelCurrent->geometry();
        int delta = 50;
        qreal aspect = static_cast<qreal>(orig.width()) / orig.height();
        int newW = orig.width() - delta;
        int newH = static_cast<int>(newW / aspect);
        QRect target(orig.x() + delta / 2,
                     orig.y() + (orig.height() - newH) / 2,
                     newW, newH);

        QPropertyAnimation *shrink = new QPropertyAnimation(m_labelCurrent, "geometry");
        shrink->setDuration(500);
        shrink->setStartValue(orig);
        shrink->setEndValue(target);
        shrink->start(QAbstractAnimation::DeleteWhenStopped);
        return;  // 此动画不使用 group，单独启动
    }
    case 5: {  // 颤抖（快速微幅振动，4px 幅度，重复 10 次）
        QPoint base = m_labelCurrent->pos();
        int offset = 4;
        int count = 10;

        for (int i = 0; i < count; i++) {
            auto *up    = new QPropertyAnimation(m_labelCurrent, "pos");
            up->setDuration(20);
            up->setStartValue(base);
            up->setEndValue(base + QPoint(0, -offset));

            auto *down  = new QPropertyAnimation(m_labelCurrent, "pos");
            down->setDuration(40);
            down->setStartValue(base + QPoint(0, -offset));
            down->setEndValue(base + QPoint(0, offset));

            auto *left  = new QPropertyAnimation(m_labelCurrent, "pos");
            left->setDuration(20);
            left->setStartValue(base + QPoint(0, offset));
            left->setEndValue(base + QPoint(-offset, 0));

            auto *right = new QPropertyAnimation(m_labelCurrent, "pos");
            right->setDuration(40);
            right->setStartValue(base + QPoint(-offset, 0));
            right->setEndValue(base + QPoint(offset, 0));

            auto *center = new QPropertyAnimation(m_labelCurrent, "pos");
            center->setDuration(20);
            center->setStartValue(base + QPoint(offset, 0));
            center->setEndValue(base);

            group->addAnimation(up);
            group->addAnimation(down);
            group->addAnimation(left);
            group->addAnimation(right);
            group->addAnimation(center);
        }
        break;
    }
    default:
        delete group;
        return;
    }

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// 公共方法
// ============================================================================

/** @brief 重置窗口位置到屏幕右下角（距边缘 50px） */
void TachieWindow::resetPosition()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->availableGeometry();
        move(geo.right() - width() - 50, geo.bottom() - height() - 50);
    } else {
        move(0, 0);
    }
}

/** @brief 设置窗口是否始终置顶（会触发窗口重建，需要重新 show） */
void TachieWindow::setAlwaysOnTop(bool onTop)
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (onTop) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    show();  // setWindowFlags 会隐藏窗口，必须重新显示
}

/** @brief 动态修改缩放比例，立即重新加载当前表情图片 */
void TachieWindow::setScale(int scalePercent)
{
    m_scalePercent = scalePercent;
    // 以新的缩放比例重新加载当前表情
    if (!m_currentEmotion.isEmpty()) {
        QPixmap pix = loadAndScaleImage(m_currentEmotion);
        if (!pix.isNull()) {
            m_labelCurrent->setPixmap(pix);
            setFixedSize(pix.size());
        }
    }
}

/** @brief 返回所有可用的情绪名列表（从 PNG 文件名提取） */
QStringList TachieWindow::availableEmotions() const
{
    return m_emotionList;
}

/**
 * @brief 切换到另一个立绘角色资源目录
 *
 * 清空现有表情列表和动画映射，重新加载新目录的配置，
 * 并显示默认情绪（"正常"或列表第一项）。
 */
void TachieWindow::setResourceDir(const QString &resourceDir)
{
    if (resourceDir == m_resourceDir) return;
    m_resourceDir = resourceDir;
    m_emotionList.clear();
    m_animMap.clear();
    loadConfig();
    if (!m_emotionList.isEmpty()) {
        QString target = m_emotionList.contains(m_defaultEmotion)
            ? m_defaultEmotion : m_emotionList.first();
        changeExpression(target);
    }
}

// ============================================================================
// 鼠标事件 —— 拖拽移动窗口
// ============================================================================

/** @brief 鼠标左键按下，记录偏移量，开始拖拽 */
void TachieWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isLeftPressDown = true;
        m_movePoint = event->globalPos() - frameGeometry().topLeft();
    }
}

/** @brief 鼠标移动中，按偏移量同步移动窗口位置 */
void TachieWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isLeftPressDown) {
        move(event->globalPos() - m_movePoint);
        event->accept();
    }
}

/** @brief 鼠标左键释放，结束拖拽，发射 positionChanged 信号保存位置 */
void TachieWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isLeftPressDown = false;
        emit positionChanged(x(), y());
    }
}

// ============================================================================
// 右键菜单
// ============================================================================

/** @brief 右键弹出菜单：重置位置 / 隐藏立绘 */
void TachieWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *resetAct = menu.addAction(QStringLiteral("重置位置"));
    QAction *hideAct  = menu.addAction(QStringLiteral("隐藏立绘"));

    QAction *selected = menu.exec(event->globalPos());
    if (selected == resetAct) {
        resetPosition();
    } else if (selected == hideAct) {
        hide();
    }
}
