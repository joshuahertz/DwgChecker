#pragma once
// Student identity parsed from submission file names (Canvas bulk download by default).

#include <QRegularExpression>
#include <QString>

namespace dc {

struct StudentIdentity {
    QString fileName;  // bare file name
    QString name;      // "smithjane"; falls back to the bare file name
    QString uid, sid;  // Canvas user / submission ids
    QString orig;      // original file name the student uploaded
    bool parsed = false;
    bool late = false;
};

class IdentityParser {
public:
    explicit IdentityParser(const QString& regex);
    bool isValid() const { return m_re.isValid(); }
    QString errorString() const { return m_re.errorString(); }
    StudentIdentity parse(const QString& fileName) const;

private:
    QRegularExpression m_re;
};

// Lower-case letters only, for comparing a LastSavedBy/Author value with a student name.
QString normalizePersonName(const QString& s);
// True when `who` (LastSavedBy/Author) plausibly names the student `studentName`.
bool personMatchesStudent(const QString& who, const QString& studentName);

}  // namespace dc
