// Windows: find AutoCAD through the registry and open files through COM when an
// instance is already running (fast), otherwise launch acad.exe.
//
// Registry layout (verified on AutoCAD 2026):
//   HKLM\SOFTWARE\Autodesk\AutoCAD\R25.1\ACAD-9101:409
//     AcadLocation = C:\Program Files\Autodesk\AutoCAD 2026\
//     ProductName  = AutoCAD 2026 - English
// COM ProgIDs registered: AutoCAD.Application.25.1, AutoCAD.Application.25 (no
// version-independent ProgID on that machine).
//
// COM is used through raw IDispatch (ActiveQt is not part of every Qt kit); the
// behaviour is the same as QAxObject::dynamicCall.

#include "../Platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>

#include <algorithm>

#include <windows.h>
#include <oleauto.h>

namespace dc::platform {

QString bundledDwgReadPath() {
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() + "/dwgread.exe" : QString();
}

namespace {

QString yearFromRelease(const QString& rel) {
    static const QList<QPair<QString, QString>> map = {
        {"R22.0", "2018"}, {"R23.0", "2019"}, {"R23.1", "2020"}, {"R24.0", "2021"}, {"R24.1", "2022"},
        {"R24.2", "2023"}, {"R24.3", "2024"}, {"R25.0", "2025"}, {"R25.1", "2026"}};
    for (const auto& [r, y] : map)
        if (r == rel) return y;
    return {};
}

double releaseNumber(const QString& rel) { return rel.mid(1).toDouble(); }

class ComScope {
public:
    ComScope() { m_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
private:
    HRESULT m_hr;
};

// IDispatch helpers with retry while AutoCAD is busy (RPC_E_CALL_REJECTED).
HRESULT invoke(IDispatch* d, const wchar_t* name, WORD flags, std::vector<VARIANT> args, VARIANT* result) {
    DISPID id;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    std::reverse(args.begin(), args.end());  // DISPPARAMS takes arguments right to left
    DISPPARAMS dp{args.empty() ? nullptr : args.data(), nullptr, static_cast<UINT>(args.size()), 0};
    DISPID named = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT) { dp.cNamedArgs = 1; dp.rgdispidNamedArgs = &named; }
    for (int attempt = 0; attempt < 25; ++attempt) {
        hr = d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, nullptr, nullptr);
        if (hr != RPC_E_CALL_REJECTED && hr != RPC_E_SERVERCALL_RETRYLATER) break;
        QThread::msleep(200);
    }
    return hr;
}

class WinLauncher : public IAutoCadLauncher {
public:
    QList<AutoCadInstall> detect() const override {
        QList<AutoCadInstall> out;
        for (const QString& root : {QStringLiteral("AutoCAD"), QStringLiteral("AutoCAD LT")}) {
            QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Autodesk\\") + root, QSettings::Registry64Format);
            for (const QString& rel : reg.childGroups()) {
                if (!rel.startsWith('R')) continue;
                reg.beginGroup(rel);
                for (const QString& prod : reg.childGroups()) {
                    if (!prod.startsWith(QStringLiteral("ACAD"), Qt::CaseInsensitive)) continue;
                    reg.beginGroup(prod);
                    const QString loc = reg.value(QStringLiteral("AcadLocation")).toString();
                    const QString pname = reg.value(QStringLiteral("ProductName")).toString();
                    reg.endGroup();
                    if (loc.isEmpty()) continue;
                    AutoCadInstall i;
                    i.release = rel;
                    i.name = pname.isEmpty() ? root + " " + rel : pname;
                    const QString acad = QDir(loc).filePath(QStringLiteral("acad.exe"));
                    const QString acadlt = QDir(loc).filePath(QStringLiteral("acadlt.exe"));
                    if (QFileInfo::exists(acad)) i.exe = acad;
                    else if (QFileInfo::exists(acadlt)) { i.exe = acadlt; i.lt = true; }
                    else continue;
                    i.lt = i.lt || root.endsWith(QStringLiteral("LT"));
                    const auto ym = QRegularExpression(QStringLiteral("(20\\d\\d)")).match(i.name);
                    i.year = ym.hasMatch() ? ym.captured(1) : yearFromRelease(rel);
                    if (!i.lt) i.progId = QStringLiteral("AutoCAD.Application.") + rel.mid(1);
                    if (std::none_of(out.begin(), out.end(), [&](const AutoCadInstall& o) { return o.exe == i.exe; }))
                        out << i;
                }
                reg.endGroup();
            }
        }
        std::sort(out.begin(), out.end(), [](const AutoCadInstall& a, const AutoCadInstall& b) {
            if (a.lt != b.lt) return !a.lt;  // full AutoCAD before LT of the same age
            return releaseNumber(a.release) > releaseNumber(b.release);
        });
        return out;
    }

    AutoCadInstall fromUserPath(const QString& path) const override {
        AutoCadInstall i;
        i.exe = path;
        i.lt = QFileInfo(path).fileName().compare(QStringLiteral("acadlt.exe"), Qt::CaseInsensitive) == 0;
        i.name = QFileInfo(path).dir().dirName();
        const auto ym = QRegularExpression(QStringLiteral("(20\\d\\d)")).match(i.name);
        i.year = ym.hasMatch() ? ym.captured(1) : QString();
        if (!i.lt) i.progId = QStringLiteral("AutoCAD.Application");
        return i;
    }

    bool open(const AutoCadInstall& inst, const QString& file, QString* error) override {
        const QString native = QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath());
        if (!inst.lt && openViaCom(inst, native)) return true;
        // No running instance (or LT, which has no COM automation): start a new session.
        if (!QProcess::startDetached(inst.exe, {native})) {
            if (error) *error = QStringLiteral("could not start %1").arg(inst.exe);
            return false;
        }
        return true;
    }

private:
    bool openViaCom(const AutoCadInstall& inst, const QString& file) const {
        ComScope com;
        QStringList progIds;
        if (!inst.progId.isEmpty()) progIds << inst.progId;
        if (inst.release.size() > 1) progIds << QStringLiteral("AutoCAD.Application.") + inst.release.mid(1).section('.', 0, 0);
        progIds << QStringLiteral("AutoCAD.Application");
        progIds.removeDuplicates();
        IDispatch* app = nullptr;
        for (const QString& pid : progIds) {
            CLSID clsid;
            if (FAILED(CLSIDFromProgID(reinterpret_cast<LPCOLESTR>(pid.utf16()), &clsid))) continue;
            IUnknown* unk = nullptr;
            if (FAILED(GetActiveObject(clsid, nullptr, &unk)) || !unk) continue;
            unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
            unk->Release();
            if (app) break;
        }
        if (!app) return false;

        bool ok = false;
        VARIANT docs;
        VariantInit(&docs);
        if (SUCCEEDED(invoke(app, L"Documents", DISPATCH_PROPERTYGET, {}, &docs)) && docs.vt == VT_DISPATCH && docs.pdispVal) {
            VARIANT path, ro, res;
            VariantInit(&path); VariantInit(&ro); VariantInit(&res);
            path.vt = VT_BSTR;
            path.bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(file.utf16()));
            ro.vt = VT_BOOL;
            ro.boolVal = VARIANT_TRUE;  // Documents.Open(Name, ReadOnly = True)
            ok = SUCCEEDED(invoke(docs.pdispVal, L"Open", DISPATCH_METHOD, {path, ro}, &res));
            VariantClear(&res);
            VariantClear(&path);
            VariantClear(&docs);
        }
        if (ok) {
            VARIANT vis, hwnd;
            VariantInit(&vis); VariantInit(&hwnd);
            vis.vt = VT_BOOL;
            vis.boolVal = VARIANT_TRUE;
            invoke(app, L"Visible", DISPATCH_PROPERTYPUT, {vis}, nullptr);
            if (SUCCEEDED(invoke(app, L"HWND", DISPATCH_PROPERTYGET, {}, &hwnd))) {
                VARIANT v;
                VariantInit(&v);
                if (SUCCEEDED(VariantChangeType(&v, &hwnd, 0, VT_I8))) {
                    HWND h = reinterpret_cast<HWND>(static_cast<intptr_t>(v.llVal));
                    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
                    SetForegroundWindow(h);
                }
                VariantClear(&hwnd);
            }
        }
        app->Release();
        return ok;
    }
};

}  // namespace

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<WinLauncher>(); }

}  // namespace dc::platform
