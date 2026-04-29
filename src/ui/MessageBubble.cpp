#include "MessageBubble.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMenu>
#include <QPixmap>

namespace {

QString attachmentLabelText(const Attachment &attachment)
{
    switch (attachment.type) {
    case Attachment::Image:
        return QStringLiteral("[Image] %1").arg(attachment.fileName);
    case Attachment::Document:
        return QStringLiteral("[Document] %1").arg(attachment.fileName);
    case Attachment::TextFile:
        return QStringLiteral("[Text] %1").arg(attachment.fileName);
    }

    return attachment.fileName;
}

} // namespace

MessageBubble::MessageBubble(Role role, const QString &content,
                             QWidget *parent,
                             const QString &userName,
                             const QString &assistantName,
                             int index,
                             bool favorite)
    : MessageBubble(role, content, {}, parent, userName, assistantName, index, favorite)
{
}

MessageBubble::MessageBubble(Role role, const QString &content,
                             const QList<Attachment> &attachments,
                             QWidget *parent,
                             const QString &userName,
                             const QString &assistantName,
                             int index,
                             bool favorite)
    : QWidget(parent)
    , m_role(role)
    , m_content(content)
    , m_userName(userName)
    , m_assistantName(assistantName)
    , m_attachments(attachments)
    , m_index(index)
    , m_favorite(favorite)
{
    setupUI();
}

void MessageBubble::setupUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 4);
    outerLayout->setSpacing(4);

    m_roleLabel = new QLabel(this);
    m_roleLabel->setObjectName(m_role == User ? "userRoleLabel" : "assistantRoleLabel");
    QFont roleFont = m_roleLabel->font();
    roleFont.setBold(true);
    roleFont.setPointSize(10);
    m_roleLabel->setFont(roleFont);
    refreshRoleLabel();

    m_bubbleWidget = new QWidget(this);
    m_bubbleWidget->setObjectName(m_role == User ? "userBubble" : "assistantBubble");
    m_bubbleWidget->setMaximumWidth(560);

    m_bubbleLayout = new QVBoxLayout(m_bubbleWidget);
    m_bubbleLayout->setContentsMargins(14, 10, 14, 10);
    m_bubbleLayout->setSpacing(8);

    m_contentLabel = new QLabel(m_content, m_bubbleWidget);
    m_contentLabel->setObjectName(m_role == User ? "userBubbleContent" : "assistantBubbleContent");
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextFormat(Qt::PlainText);
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_contentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_contentLabel->setVisible(!m_content.isEmpty());

    rebuildAttachmentWidgets();

    if (m_role == User) {
        m_roleLabel->setAlignment(Qt::AlignRight);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addStretch();
        bubbleRow->addWidget(m_bubbleWidget);
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    } else {
        m_roleLabel->setAlignment(Qt::AlignLeft);
        QHBoxLayout *bubbleRow = new QHBoxLayout;
        bubbleRow->addWidget(m_bubbleWidget);
        bubbleRow->addStretch();
        outerLayout->addWidget(m_roleLabel);
        outerLayout->addLayout(bubbleRow);
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &MessageBubble::onContextMenu);
}

void MessageBubble::rebuildAttachmentWidgets()
{
    while (m_bubbleLayout->count() > 0) {
        QLayoutItem *item = m_bubbleLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    for (const Attachment &attachment : m_attachments) {
        if (QWidget *attachmentWidget = createAttachmentWidget(attachment)) {
            m_bubbleLayout->addWidget(attachmentWidget);
        }
    }

    m_bubbleLayout->addWidget(m_contentLabel);
}

QWidget *MessageBubble::createAttachmentWidget(const Attachment &attachment)
{
    if (attachment.type == Attachment::Image) {
        QLabel *imageLabel = new QLabel(m_bubbleWidget);
        QPixmap pixmap;
        pixmap.loadFromData(attachment.fileData);
        if (!pixmap.isNull()) {
            if (pixmap.width() > 400) {
                pixmap = pixmap.scaledToWidth(400, Qt::SmoothTransformation);
            }
            imageLabel->setPixmap(pixmap);
        } else {
            imageLabel->setText(attachmentLabelText(attachment));
        }
        imageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        imageLabel->setWordWrap(true);
        return imageLabel;
    }

    QLabel *label = new QLabel(attachmentLabelText(attachment), m_bubbleWidget);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(
        "background: rgba(128,128,128,0.08);"
        "border-radius: 6px;"
        "padding: 6px 10px;");
    return label;
}

void MessageBubble::appendText(const QString &text)
{
    m_content += text;
    m_contentLabel->setText(m_content);
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

void MessageBubble::setContent(const QString &content)
{
    m_content = content;
    m_contentLabel->setText(m_content);
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

void MessageBubble::setAttachments(const QList<Attachment> &attachments)
{
    m_attachments = attachments;
    rebuildAttachmentWidgets();
    m_contentLabel->setVisible(!m_content.isEmpty());
    updateGeometry();
}

QString MessageBubble::content() const
{
    return m_content;
}

MessageBubble::Role MessageBubble::role() const
{
    return m_role;
}

void MessageBubble::setRoleName(const QString &name)
{
    if (m_role == User) {
        m_userName = name;
    } else {
        m_assistantName = name;
    }
    refreshRoleLabel();
}

int MessageBubble::messageIndex() const
{
    return m_index;
}

void MessageBubble::setMessageIndex(int index)
{
    m_index = index;
}

bool MessageBubble::isFavorite() const
{
    return m_favorite;
}

void MessageBubble::setFavorite(bool favorite)
{
    if (m_favorite == favorite) {
        return;
    }
    m_favorite = favorite;
    refreshRoleLabel();
}

void MessageBubble::refreshRoleLabel()
{
    const QString name = (m_role == User) ? m_userName : m_assistantName;
    m_roleLabel->setText(m_favorite ? QStringLiteral("* ") + name : name);
}

void MessageBubble::onContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("Copy"));
    QAction *favAction = menu.addAction(m_favorite ? tr("Unfavorite") : tr("Favorite"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("Delete from here"));

    QAction *selected = menu.exec(mapToGlobal(pos));
    if (!selected) {
        return;
    }

    if (selected == copyAction) {
        QApplication::clipboard()->setText(m_content);
    } else if (selected == favAction) {
        emit favoriteToggleRequested(m_index);
    } else if (selected == deleteAction) {
        emit deleteFromHereRequested(m_index);
    }
}
