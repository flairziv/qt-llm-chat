#pragma once
#include <QWidget>
#include <QLabel>
#include <QPoint>
#include <QMap>
#include <QStringList>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QSequentialAnimationGroup;

/**
 * @brief 立绘显示窗口 -- 独立的无边框透明置顶窗口
 *
 * 功能：
 * - 从指定目录加载以情绪命名的 PNG 图片（如 高兴.png、生气.png）
 * - 切换表情时播放淡入淡出过渡动画
 * - 根据 anim.ini 配置播放 5 种动画效果
 * - 支持鼠标拖拽移动窗口位置
 * - 右键菜单：重置位置 / 隐藏窗口
 *
 * 布局结构（3 个 QLabel 堆叠在 QGridLayout 同一格中）：
 *   m_labelPrevious  -- 旧表情（淡出用）
 *   m_labelCurrent   -- 当前表情（淡入用）
 *   m_labelParticle  -- 粒子特效层（预留）
 *
 * 由 MainWindow 创建并管理，通过 changeExpression() 驱动表情切换。
 */
class TachieWindow : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param resourceDir 立绘资源目录路径（包含 PNG + config.ini + anim.ini）
     * @param parent      父 widget（传 nullptr 以创建独立顶层窗口）
     */
    explicit TachieWindow(const QString &resourceDir, QWidget *parent = nullptr);
    ~TachieWindow();

    /** @brief 切换到指定情绪的表情（触发淡入淡出 + 动画） */
    void changeExpression(const QString &emotionName);

    /** @brief 重置窗口位置到屏幕右下角 */
    void resetPosition();

    /** @brief 设置是否置顶 */
    void setAlwaysOnTop(bool onTop);

    /** @brief 设置缩放比例（百分比，100 = 原始大小） */
    void setScale(int scalePercent);

    /** @brief 获取所有可用的情绪名称列表（从 PNG 文件名提取） */
    QStringList availableEmotions() const;

    /** @brief 获取当前显示的情绪名称 */
    QString currentEmotion() const { return m_currentEmotion; }

    /** @brief 切换立绘资源目录（更换角色） */
    void setResourceDir(const QString &resourceDir);

signals:
    /** @brief 拖拽释放后发射，通知外部保存位置 */
    void positionChanged(int x, int y);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void setupUI();                                          // 初始化界面布局
    void loadConfig();                                       // 加载配置文件和情绪列表
    QPixmap loadAndScaleImage(const QString &emotionName);   // 加载并缩放指定情绪的图片
    void playFadeTransition(const QPixmap &newPixmap);       // 播放淡入淡出过渡动画
    void playAnimation(int animationType);                   // 播放指定类型的动画

    // --- 界面控件 ---
    QLabel *m_labelCurrent = nullptr;    // 当前表情图层（淡入）
    QLabel *m_labelPrevious = nullptr;   // 旧表情图层（淡出）
    QLabel *m_labelParticle = nullptr;   // 粒子特效图层（预留）

    // --- 状态数据 ---
    QString m_resourceDir;               // 资源目录路径
    QString m_currentEmotion;            // 当前正在显示的情绪名
    QString m_defaultEmotion;            // 默认情绪名（"正常"）
    int m_scalePercent = 100;            // 图片缩放百分比
    QMap<QString, int> m_animMap;        // 情绪 → 动画类型映射（1-5）
    QStringList m_emotionList;           // 所有可用的情绪名列表

    // --- 拖拽相关 ---
    QPoint m_movePoint;                  // 鼠标按下时的偏移量
    bool m_isLeftPressDown = false;      // 左键是否按下
};
