#include "StudentIdentity.h"

#include <QFileInfo>

namespace dc {

IdentityParser::IdentityParser(const QString& regex) : m_re(regex) {}

StudentIdentity IdentityParser::parse(const QString& fileName) const {
    StudentIdentity id;
    id.fileName = QFileInfo(fileName).fileName();
    id.name = QFileInfo(id.fileName).completeBaseName();
    id.late = id.fileName.contains(QStringLiteral("_LATE_"));
    if (!m_re.isValid()) return id;
    // Composed form, so an accent stored as a separate mark (as macOS stores names) reads
    // as the same name and groups with the student's other files (D80).
    const auto m = m_re.match(id.fileName.normalized(QString::NormalizationForm_C));
    if (!m.hasMatch()) return id;
    const QString name = m.captured(QStringLiteral("name"));
    if (name.isEmpty()) return id;
    id.parsed = true;
    id.name = name;
    id.uid = m.captured(QStringLiteral("uid"));
    id.sid = m.captured(QStringLiteral("sid"));
    id.orig = m.captured(QStringLiteral("orig"));
    return id;
}

}  // namespace dc
