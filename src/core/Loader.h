#pragma once
// Drawing loaders. IDrawingLoader hides how a DWG becomes JSON so the ODA SDK or
// a DXF route can replace dwgread later.

#include "DwgJson.h"
#include "Settings.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace dc {

struct LoadOutcome {
    bool ok = false;
    QString error;
    QStringList warnings;
    DwgJson json;
};

class IDrawingLoader {
public:
    virtual ~IDrawingLoader() = default;
    // Must be safe to call from several threads at once.
    virtual LoadOutcome load(const QString& path) const = 0;
    // Human-readable parser identity, logged at startup and in every CSV export.
    virtual QString parserVersion() const = 0;
};

// Runs `dwgread -O minJSON <file>` in a separate process (GPL isolation, crash
// isolation) with a per-file timeout.
class DwgReadLoader : public IDrawingLoader {
public:
    explicit DwgReadLoader(LoaderSettings settings);
    LoadOutcome load(const QString& path) const override;
    QString parserVersion() const override { return m_version; }
    bool isUsable(QString* why = nullptr) const;

    // "dwgread 0.14 (sha256 1ad7e153...)" or an empty string with *error set.
    static QString probeVersion(const QString& exe, QString* error);

private:
    bool runDwgRead(const QString& file, QByteArray* out, QString* error, QStringList* warnings) const;
    bool convertWithOda(const QString& file, QString* outFile, QString* error, const QString& workDir) const;

    LoaderSettings m_settings;
    QString m_version;
    QString m_versionError;
};

// Reads JSON that was produced earlier by dwgread (tests, fixtures, offline use).
class JsonFileLoader : public IDrawingLoader {
public:
    LoadOutcome load(const QString& path) const override;
    QString parserVersion() const override { return QStringLiteral("pre-dumped JSON"); }
};

// .json -> JsonFileLoader, anything else -> DwgReadLoader.
class AutoLoader : public IDrawingLoader {
public:
    explicit AutoLoader(LoaderSettings settings) : m_dwg(std::move(settings)) {}
    LoadOutcome load(const QString& path) const override;
    QString parserVersion() const override { return m_dwg.parserVersion(); }
    const DwgReadLoader& dwgRead() const { return m_dwg; }

private:
    DwgReadLoader m_dwg;
    JsonFileLoader m_json;
};

QByteArray sha256File(const QString& path, qint64* size = nullptr);

}  // namespace dc
