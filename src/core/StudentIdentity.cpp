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
    const auto m = m_re.match(id.fileName);
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
