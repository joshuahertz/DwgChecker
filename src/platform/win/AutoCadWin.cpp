// Windows: open a file in the AutoCAD that is already running, through its COM automation
// (Documents.Open, read-only). The app never starts AutoCAD (D96): an AutoCAD started from
// the Store version would run inside its package.
//
// COM ProgIDs registered by AutoCAD 2026: AutoCAD.Application.25.1 and AutoCAD.Application.25
// (no version-independent ProgID on that machine), so every release's ProgIDs are tried.
// AutoCAD LT has no COM automation and is not supported: only the full AutoCAD is found.
//
// COM is used through raw IDispatch (ActiveQt is not part of every Qt kit); the
// behaviour is the same as QAxObject::dynamicCall.

#include "../Platform.h"

#include <QCoreApplication>
#include <QStringList>
#include <QThread>

#include <algorithm>

#include <windows.h>
#include <oleauto.h>

namespace dc::platform {

QString bundledDwgReadPath() {
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() + "/dwgread.exe" : QString();
}

namespace {

class ComScope {
public:
    ComScope() { m_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
private:
    HRESULT m_hr;
};

// IDispatch helpers with retry while AutoCAD is busy (RPC_E_CALL_REJECTED).
HRESULT invoke(IDispatch* d, const wchar_t* name, WORD flags, std::vector<VARIANT> args, VARIANT* result,
               EXCEPINFO* exception = nullptr) {
    DISPID id;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    std::reverse(args.begin(), args.end());  // DISPPARAMS takes arguments right to left
    DISPPARAMS dp{args.empty() ? nullptr : args.data(), nullptr, static_cast<UINT>(args.size()), 0};
    DISPID named = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT) { dp.cNamedArgs = 1; dp.rgdispidNamedArgs = &named; }
    for (int attempt = 0; attempt < 25; ++attempt) {
        hr = d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, exception, nullptr);
        if (hr != RPC_E_CALL_REJECTED && hr != RPC_E_SERVERCALL_RETRYLATER) break;
        QThread::msleep(200);
    }
    return hr;
}

// The COM ProgIDs AutoCAD registers, newest first: AutoCAD.Application.25.1, .25, ... back to
// 2018 (R22), and the version-independent one some installs have.
QStringList progIds() {
    QStringList ids;
    for (int major = 30; major >= 22; --major) {
        for (int minor = 3; minor >= 0; --minor) ids << QStringLiteral("AutoCAD.Application.%1.%2").arg(major).arg(minor);
        ids << QStringLiteral("AutoCAD.Application.%1").arg(major);
    }
    ids << QStringLiteral("AutoCAD.Application");
    return ids;
}

// The running AutoCAD's Application object (AddRef'd), or null when none is running.
IDispatch* runningApplication() {
    for (const QString& pid : progIds()) {
        CLSID clsid;
        if (FAILED(CLSIDFromProgID(reinterpret_cast<LPCOLESTR>(pid.utf16()), &clsid))) continue;  // not installed
        IUnknown* unk = nullptr;
        if (FAILED(GetActiveObject(clsid, nullptr, &unk)) || !unk) continue;  // installed, not running
        IDispatch* app = nullptr;
        unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
        unk->Release();
        if (app) return app;
    }
    return nullptr;
}

// Why a call into AutoCAD failed, for the user (D97): busy only when it said so; else AutoCAD's
// own message, else the error code.
QString failureText(HRESULT hr, EXCEPINFO& ex) {
    if (ex.pfnDeferredFillIn) ex.pfnDeferredFillIn(&ex);
    QString text;
    if (hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER)
        text = QStringLiteral("AutoCAD is busy (a command or dialog is open): press Esc there and try again");
    else if (hr == DISP_E_EXCEPTION && ex.bstrDescription && *ex.bstrDescription)
        text = QStringLiteral("AutoCAD says: %1").arg(QString::fromWCharArray(ex.bstrDescription).trimmed());
    else
        text = QStringLiteral("error 0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
    SysFreeString(ex.bstrSource);
    SysFreeString(ex.bstrDescription);
    SysFreeString(ex.bstrHelpFile);
    ex = EXCEPINFO{};
    return text;
}

class WinLauncher : public IAutoCadLauncher {
public:
    // Only looks for it: no call into AutoCAD, which could wait on a busy one (D97).
    QString runningAutoCad() const override {
        ComScope com;
        IDispatch* app = runningApplication();
        if (!app) return {};
        app->Release();
        return QStringLiteral("AutoCAD");
    }

    bool openInRunning(const QString& file, QString* error) override {
        ComScope com;
        IDispatch* app = runningApplication();
        if (!app) {
            if (error) *error = QStringLiteral("AutoCAD is not running");
            return false;
        }
        bool ok = false;
        EXCEPINFO ex{};
        VARIANT docs;
        VariantInit(&docs);
        HRESULT hr = invoke(app, L"Documents", DISPATCH_PROPERTYGET, {}, &docs, &ex);
        if (SUCCEEDED(hr) && docs.vt == VT_DISPATCH && docs.pdispVal) {
            VARIANT path, ro, res;
            VariantInit(&path); VariantInit(&ro); VariantInit(&res);
            path.vt = VT_BSTR;
            path.bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(file.utf16()));
            ro.vt = VT_BOOL;
            ro.boolVal = VARIANT_TRUE;  // Documents.Open(Name, ReadOnly = True)
            hr = invoke(docs.pdispVal, L"Open", DISPATCH_METHOD, {path, ro}, &res, &ex);
            ok = SUCCEEDED(hr);
            VariantClear(&res);
            VariantClear(&path);
        }
        VariantClear(&docs);
        if (ok) {  // bring AutoCAD to the front
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
        } else if (error) {
            *error = failureText(SUCCEEDED(hr) ? E_UNEXPECTED : hr, ex);
        } else {
            failureText(hr, ex);  // frees the strings
        }
        app->Release();
        return ok;
    }
};

}  // namespace

std::unique_ptr<IAutoCadLauncher> createAutoCadLauncher() { return std::make_unique<WinLauncher>(); }

}  // namespace dc::platform
