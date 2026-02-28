#include "SessionListWidget.h"
#include "core/ChatSession.h"
#include <QMenu>

// ============================================================================
// 构造函数
// ============================================================================

SessionListWidget::SessionListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setupUI();
}

// ============================================================================
// UI 初始化
// ============================================================================

/**
 * @brief 配置列表控件的基本属性和信号连接
 *
 * - CustomContextMenu：启用右键自定义菜单（而非系统默认菜单）
 * - SingleSelection：同一时间只能选中一个会话
 * - NoFrame：去掉列表边框，与左侧面板融为一体
 */
void SessionListWidget::setupUI()
{
    setContextMenuPolicy(Qt::CustomContextMenu);       // 启用自定义右键菜单
    setSelectionMode(QAbstractItemView::SingleSelection); // 单选模式
    setFrameShape(QFrame::NoFrame);                    // 无边框

    // 点击列表项 → onItemClicked
    connect(this, &QListWidget::itemClicked, this, &SessionListWidget::onItemClicked);
    // 右键请求菜单 → onContextMenu
    connect(this, &QListWidget::customContextMenuRequested, this, &SessionListWidget::onContextMenu);
}

// ============================================================================
// 会话管理
// ============================================================================

/**
 * @brief 将一个新的会话添加到列表顶部
 *
 * 每个 QListWidgetItem 存储的数据：
 *   - 显示文本（setText）：会话标题
 *   - Qt::UserRole（setData）：会话 ID（用于后续查找和信号传递）
 *   - ToolTip：最后更新时间（鼠标悬停显示）
 *
 * 新会话 insertItem(0, ...) 插入到列表最顶部，并自动选中。
 *
 * 同时连接 ChatSession::titleChanged 信号，当会话标题变化时
 * （如首条消息作为标题），自动更新列表中对应项的显示文本。
 */
void SessionListWidget::addSession(ChatSession *session)
{
    auto *item = new QListWidgetItem(session->title());
    item->setData(Qt::UserRole, session->id());   // 将会话 ID 存储在 UserRole 中
    item->setToolTip(session->updatedAt().toString("yyyy-MM-dd hh:mm"));
    insertItem(0, item);     // 插入到列表顶部（最新的在最上面）
    setCurrentItem(item);    // 自动选中新创建的会话

    // 监听会话标题变化，实时更新列表项文本
    connect(session, &ChatSession::titleChanged, this, [this, session](const QString &title) {
        for (int i = 0; i < count(); ++i) {
            QListWidgetItem *it = this->item(i);
            if (it->data(Qt::UserRole).toString() == session->id()) {
                it->setText(title);  // 找到对应项，更新显示文本
                break;
            }
        }
    });
}

/**
 * @brief 按会话 ID 从列表中移除对应项
 *
 * 遍历所有项，通过 UserRole 中存储的 ID 匹配。
 * takeItem() 将项从列表中取出，delete 释放内存。
 */
void SessionListWidget::removeSession(const QString &id)
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(Qt::UserRole).toString() == id) {
            delete takeItem(i);
            return;
        }
    }
}

/**
 * @brief 按会话 ID 高亮选中对应项（不触发点击信号）
 */
void SessionListWidget::setActiveSession(const QString &id)
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(Qt::UserRole).toString() == id) {
            setCurrentRow(i);
            return;
        }
    }
}

/**
 * @brief 清空列表并重新填充所有会话
 *
 * 在应用启动时加载历史会话时调用。
 */
void SessionListWidget::refreshList(const QList<ChatSession*> &sessions)
{
    clear();
    for (auto *session : sessions) {
        addSession(session);
    }
}

// ============================================================================
// 槽函数 —— 用户交互
// ============================================================================

/**
 * @brief 列表项被点击时，提取其中的会话 ID 并发射 sessionSelected 信号
 */
void SessionListWidget::onItemClicked(QListWidgetItem *item)
{
    if (item) {
        emit sessionSelected(item->data(Qt::UserRole).toString());
    }
}

/**
 * @brief 右键上下文菜单处理
 *
 * 在点击位置弹出菜单，当前仅包含 "Delete" 选项。
 * 用户选择删除后，发射 sessionDeleteRequested 信号，
 * 由 MainWindow 负责实际的会话删除操作。
 *
 * mapToGlobal(pos) 将控件内的相对坐标转换为屏幕绝对坐标，
 * 确保菜单弹出在鼠标点击位置。
 */
void SessionListWidget::onContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = itemAt(pos);  // 获取右键点击位置的列表项
    if (!item) return;                     // 点击空白区域，忽略

    QString id = item->data(Qt::UserRole).toString();

    // 构建并弹出右键菜单
    QMenu menu(this);
    QAction *deleteAction = menu.addAction("Delete");

    QAction *selected = menu.exec(mapToGlobal(pos));  // 阻塞式弹出菜单
    if (selected == deleteAction) {
        emit sessionDeleteRequested(id);
    }
}
