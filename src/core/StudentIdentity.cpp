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

QString normalizePersonName(const QString& s) {
    QString out;
    for (QChar c : s.toLower())
        if (c.isLetter()) out += c;
    return out;
}

bool personMatchesStudent(const QString& who, const QString& studentName) {
    const QString a = normalizePersonName(who), b = normalizePersonName(studentName);
    if (a.size() < 3 || b.size() < 3) return false;
    if (a == b) return true;
    // "Jane Smith" vs "smithjane": same letters in a different order of the two parts.
    const QStringList parts = who.toLower().split(QRegularExpression(QStringLiteral("[^a-z]+")), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        QString rev;
        for (auto it = parts.crbegin(); it != parts.crend(); ++it) rev += *it;
        if (rev == b) return true;
    }
    // A login contained in the name (or vice versa), when long enough to be meaningful.
    if (a.size() >= 5 && b.contains(a)) return true;
    if (b.size() >= 5 && a.contains(b)) return true;
    return false;
}

}  // namespace dc
