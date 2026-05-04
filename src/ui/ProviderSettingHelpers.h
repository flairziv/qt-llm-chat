#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QObject>
#include <QString>

#include <functional>

/**
 * @brief Provider 设置页通用控件绑定辅助
 *
 * 三家 SettingPage（Claude / OpenAI / Gemini）的 loadSettings + connectAutoSave
 * 是同一套样板：读 AppSettings → 填控件 → connect textChanged 写回。
 * 这里把它收敛为三个 inline helper，子类构造函数里直接 bind，不再各自维护
 * loadSettings / connectAutoSave。
 *
 * Header-only：避免引入 .cpp 文件 + 修改 CMakeLists 的 SOURCES 列表。
 */
namespace ProviderSettingHelpers {

using StringGetter = std::function<QString()>;
using StringSetter = std::function<void(const QString&)>;
using StringNormalizer = std::function<QString(const QString&)>;

/** @brief QLineEdit ↔ AppSettings 字符串字段的双向绑定 */
inline void bindLineEdit(QLineEdit *edit, StringGetter getter, StringSetter setter)
{
    edit->setText(getter());
    QObject::connect(edit, &QLineEdit::textChanged, edit,
                     [setter = std::move(setter)](const QString &t) { setter(t); });
}

/**
 * @brief QComboBox ↔ AppSettings 字符串字段的双向绑定
 *
 * 当前值在下拉项中找不到时（例如用户配过一个非内置 model 名），自动 addItem 保留
 * 该值并选中，避免回写时丢失。可选传 normalize：用于在写回前规范化文本。
 */
inline void bindComboBox(QComboBox *combo, StringGetter getter, StringSetter setter,
                         StringNormalizer normalize = {})
{
    const QString val = getter();
    int idx = combo->findText(val);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else if (!val.isEmpty()) {
        combo->addItem(val);
        combo->setCurrentText(val);
    }
    QObject::connect(combo, &QComboBox::currentTextChanged, combo,
                     [setter = std::move(setter),
                      normalize = std::move(normalize)](const QString &t) {
                         setter(normalize ? normalize(t) : t);
                     });
}

/**
 * @brief 推理强度 combo 的专用绑定
 *
 * 内部映射：AppSettings 用空字符串表示「default」，UI combo 用 "default" 文字项。
 */
inline void bindReasoningComboBox(QComboBox *combo, StringGetter getter, StringSetter setter)
{
    QString val = getter();
    if (val.isEmpty()) {
        val = QStringLiteral("default");
    }

    int idx = combo->findText(val);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else {
        combo->addItem(val);
        combo->setCurrentText(val);
    }
    QObject::connect(combo, &QComboBox::currentTextChanged, combo,
                     [setter = std::move(setter)](const QString &t) {
                         setter(t == QLatin1String("default") ? QString() : t);
                     });
}

} // namespace ProviderSettingHelpers
