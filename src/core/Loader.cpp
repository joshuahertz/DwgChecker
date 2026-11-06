#include "Loader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace dc {

QByteArray sha256File(const QString& path, qint64* size) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    if (size) *size = f.size();
    return h.result().toHex();
}

static bool isAscii(const QString& s) {
    for (QChar c : s)
        if (c.unicode() > 0x7e) return false;
    return true;
}

static QString tail(const QByteArray& b, int n = 400) {
    QString s = QString::fromLocal8Bit(b).trimmed();
    if (s.size() > n) s = QStringLiteral("…") + s.right(n);
    return s;
}

DwgReadLoader::DwgReadLoader(LoaderSettings settings) : m_settings(std::move(settings)) {
    if (!m_settings.dwgreadPath.isEmpty()) m_version = probeVersion(m_settings.dwgreadPath, &m_versionError);
    else m_versionError = QStringLiteral("dwgread was not found");
}

bool DwgReadLoader::isUsable(QString* why) const {
    if (!m_version.isEmpty()) return true;
    if (why) *why = m_versionError;
    return false;
}

QString DwgReadLoader::probeVersion(const QString& exe, QString* error) {
    if (!QFileInfo::exists(exe)) {
        if (error) *error = QStringLiteral("dwgread not found at %1").arg(exe);
        return {};
    }
    QProcess p;
    p.start(exe, {QStringLiteral("--version")});
    if (!p.waitForStarted(10000) || !p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(2000);
        if (error) *error = QStringLiteral("could not run %1 --version").arg(exe);
        return {};
    }
    const QString first = QString::fromLocal8Bit(p.readAllStandardOutput()).split('\n').value(0).trimmed();
    const QByteArray sha = sha256File(exe);
    return QStringLiteral("%1 (sha256 %2)").arg(first.isEmpty() ? QStringLiteral("dwgread ?") : first,
                                                QString::fromLatin1(sha.left(16)));
}

bool DwgReadLoader::runDwgRead(const QString& file, QByteArray* out, QString* error, QStringList* warnings) const {
    QProcess p;
    p.setProgram(m_settings.dwgreadPath);
    p.setArguments({QStringLiteral("-O"), QStringLiteral("minJSON"), QDir::toNativeSeparators(file)});
    p.start();
    if (!p.waitForStarted(15000)) {
        *error = QStringLiteral("could not start dwgread: %1").arg(p.errorString());
        return false;
    }
    const int timeoutMs = std::max(1, m_settings.timeoutSec) * 1000;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(5000);
        *error = QStringLiteral("dwgread timed out after %1 s").arg(m_settings.timeoutSec);
        return false;
    }
    *out = p.readAllStandardOutput();
    const QByteArray err = p.readAllStandardError();
    if (p.exitStatus() == QProcess::CrashExit) {
        *error = QStringLiteral("dwgread crashed: %1").arg(tail(err));
        return false;
    }
    if (out->trimmed().isEmpty()) {
        *error = QStringLiteral("dwgread produced no output (exit %1): %2").arg(p.exitCode()).arg(tail(err));
        return false;
    }
    if (p.exitCode() != 0)
        warnings->append(QStringLiteral("dwgread exited with code %1: %2").arg(p.exitCode()).arg(tail(err, 200)));
    return true;
}

bool DwgReadLoader::convertWithOda(const QString& file, QString* outFile, QString* error, const QString& workDir) const {
    // ODAFileConverter <in dir> <out dir> <version> <type> <recurse> <audit> [<filter>]
    const QString inDir = workDir + "/oda_in", outDir = workDir + "/oda_out";
    QDir().mkpath(inDir);
    QDir().mkpath(outDir);
    const QString name = QStringLiteral("input.dwg");
    QFile::remove(inDir + "/" + name);
    if (!QFile::copy(file, inDir + "/" + name)) {
        *error = QStringLiteral("could not stage file for ODA File Converter");
        return false;
    }
    QProcess p;
    p.start(m_settings.odaConverterPath,
            {QDir::toNativeSeparators(inDir), QDir::toNativeSeparators(outDir), QStringLiteral("ACAD2018"),
             QStringLiteral("DWG"), QStringLiteral("0"), QStringLiteral("1"), name});
    if (!p.waitForStarted(15000) || !p.waitForFinished(std::max(1, m_settings.timeoutSec) * 2000)) {
        p.kill();
        p.waitForFinished(5000);
        *error = QStringLiteral("ODA File Converter failed or timed out");
        return false;
    }
    *outFile = outDir + "/" + name;
    if (!QFileInfo::exists(*outFile)) {
        *error = QStringLiteral("ODA File Converter produced no output");
        return false;
    }
    return true;
}

LoadOutcome DwgReadLoader::load(const QString& path) const {
    LoadOutcome r;
    QString why;
    if (!isUsable(&why)) { r.error = why; return r; }
    if (!QFileInfo::exists(path)) { r.error = QStringLiteral("file not found"); return r; }

    QTemporaryDir tmp;
    QString file = path;
    if (!isAscii(QDir::toNativeSeparators(path))) {
        // dwgread opens files with narrow fopen(); stage non-ASCII paths under an ASCII name.
        file = tmp.path() + "/input.dwg";
        if (!QFile::copy(path, file)) { r.error = QStringLiteral("could not stage file with non-ASCII path"); return r; }
    }

    QByteArray out;
    QString err;
    bool ok = runDwgRead(file, &out, &err, &r.warnings);
    if (ok) {
        ok = r.json.parse(out, &err);
    }
    if (!ok && !m_settings.odaConverterPath.isEmpty() && tmp.isValid()) {
        QString converted, odaErr;
        const QString firstError = err;
        if (convertWithOda(path, &converted, &odaErr, tmp.path())) {
            out.clear();
            err.clear();
            ok = runDwgRead(converted, &out, &err, &r.warnings) && r.json.parse(out, &err);
            if (ok)
                r.warnings << QStringLiteral("dwgread could not read the file (%1); read a copy rewritten by ODA File "
                                             "Converter instead, so metadata may reflect the converter")
                                  .arg(firstError);
        } else {
            err = firstError + "; ODA fallback: " + odaErr;
        }
    }
    r.ok = ok;
    if (!ok) r.error = err;
    return r;
}

LoadOutcome JsonFileLoader::load(const QString& path) const {
    LoadOutcome r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { r.error = QStringLiteral("cannot open %1").arg(path); return r; }
    QString err;
    r.ok = r.json.parse(f.readAll(), &err);
    if (!r.ok) r.error = err;
    return r;
}

LoadOutcome AutoLoader::load(const QString& path) const {
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) return m_json.load(path);
    return m_dwg.load(path);
}

}  // namespace dc
