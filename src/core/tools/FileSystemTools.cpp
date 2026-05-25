#include "FileSystemTools.h"

#include "core/Tool.h"
#include "core/ToolRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace {

// 上限 1 MiB：超过此值的"文本"基本不是给 LLM 读的，硬截再回填会浪费 token 还可能
// 截在 UTF-8 多字节中间产生乱码。模型看到 isError 自己决定要不要分片读。
constexpr qint64 kMaxReadFileBytes = 1 * 1024 * 1024;

/**
 * @brief read_file(path: string) → 文件文本内容
 *
 * 仅为文本设计。二进制文件用 QString::fromUtf8 解码可能产生 replacement char，
 * 但仍然返回（isError=false）——把判断权交给模型，比"应用层猜文件类型再拒绝"
 * 更灵活。失败场景只有：path 缺失 / 文件打不开 / 超过 1 MiB。
 */
ToolResult readFile(const QJsonObject &args)
{
    ToolResult r;

    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        r.isError = true;
        r.content = QStringLiteral("Missing required argument: path");
        return r;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.isError = true;
        r.content = QStringLiteral("Cannot open file %1: %2").arg(path, f.errorString());
        return r;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    if (bytes.size() > kMaxReadFileBytes) {
        r.isError = true;
        r.content = QStringLiteral(
                        "File too large (%1 bytes); read_file caps at %2 bytes. "
                        "Consider asking the user to inspect or trim the file first.")
                        .arg(bytes.size())
                        .arg(kMaxReadFileBytes);
        return r;
    }

    r.content = QString::fromUtf8(bytes);
    return r;
}

/**
 * @brief list_directory(path: string) → 目录条目的纯文本列表
 *
 * 非递归。每行格式 "[DIR]/      大小(10位右对齐)  文件名"，dirs 排在前面。
 * 空目录返回字面量 "(empty)"，让模型区分"成功但空"与"出错"。
 */
ToolResult listDirectory(const QJsonObject &args)
{
    ToolResult r;

    const QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        r.isError = true;
        r.content = QStringLiteral("Missing required argument: path");
        return r;
    }

    QDir dir(path);
    if (!dir.exists()) {
        r.isError = true;
        r.content = QStringLiteral("Directory does not exist: %1").arg(path);
        return r;
    }

    QStringList lines;
    const auto entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name);
    for (const auto &entry : entries) {
        const QString kind = entry.isDir() ? QStringLiteral("[DIR]") : QStringLiteral("     ");
        const qint64 size = entry.isDir() ? qint64(0) : entry.size();
        lines << QStringLiteral("%1  %2  %3")
                     .arg(kind)
                     .arg(size, 10)
                     .arg(entry.fileName());
    }

    r.content = lines.isEmpty() ? QStringLiteral("(empty)") : lines.join(QChar('\n'));
    return r;
}

// 构造一个 "type: object" 风格的 JSON Schema 片段（Claude tools 协议子集）。
// 工具数量积累到一定程度再抽出共用头；当前 C1 阶段每个 tools/*.cpp 各内联一份。
QJsonObject makeObjectSchema(const QJsonObject &properties, const QStringList &required)
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = properties;
    if (!required.isEmpty()) {
        QJsonArray arr;
        for (const auto &r : required) arr.append(r);
        schema[QStringLiteral("required")] = arr;
    }
    return schema;
}

QJsonObject makeStringProperty(const QString &description)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("string");
    p[QStringLiteral("description")] = description;
    return p;
}

}  // namespace

void registerFileSystemTools()
{
    {
        Tool t;
        t.name = QStringLiteral("read_file");
        t.description = QStringLiteral(
            "Read the text content of a local file and return it as a string. "
            "Capped at 1 MiB. Designed for text files; binary content may decode "
            "to replacement characters. Use list_directory first to inspect the "
            "filesystem.");
        QJsonObject props;
        props[QStringLiteral("path")] = makeStringProperty(
            QStringLiteral("Absolute filesystem path to the file to read."));
        t.inputSchema = makeObjectSchema(props, { QStringLiteral("path") });
        t.riskLevel = RiskLevel::ReadOnly;
        t.execute = readFile;
        ToolRegistry::instance().registerTool(std::move(t));
    }

    {
        Tool t;
        t.name = QStringLiteral("list_directory");
        t.description = QStringLiteral(
            "List the entries of a local directory (non-recursive). Returns a "
            "plain-text listing with [DIR] markers, file sizes, and names. "
            "Returns the literal string \"(empty)\" if the directory has no entries.");
        QJsonObject props;
        props[QStringLiteral("path")] = makeStringProperty(
            QStringLiteral("Absolute filesystem path to the directory to list."));
        t.inputSchema = makeObjectSchema(props, { QStringLiteral("path") });
        t.riskLevel = RiskLevel::ReadOnly;
        t.execute = listDirectory;
        ToolRegistry::instance().registerTool(std::move(t));
    }
}
