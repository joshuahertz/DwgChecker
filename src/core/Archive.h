#pragma once
// Submissions in a .zip (a Canvas bulk download as it comes, or a folder sent as a zip),
// read with the zlib that Qt Core carries, so no other library is needed (D81).

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace dc {

// Whether `path` names a .zip, by its suffix.
bool isZipPath(const QString& path);

// The drawings in a zip, by their paths inside it, picked as findSubmissionFiles picks them
// in a folder (the .dwg files, else the .json dumps), from any folder in the zip. Mac
// resource forks (__MACOSX/, ._name) are not drawings. Sorted by name.
struct ZipListing {
    QStringList names;   // '/' between folders
    int nestedZips = 0;  // .zip files inside it, which are not opened
    QString error;       // the zip could not be read at all
};
ZipListing listZipSubmissions(const QString& zipPath);

// Unpacks those drawings into `destDir`, in the zip's folders. ".." parts, a leading '/', and
// characters Windows does not allow in names are cleaned out, so nothing lands outside
// `destDir`; a second file with the same name (ignoring case) gets " (2)". Each file is
// checked against the zip's checksum.
struct ZipUnpacked {
    QStringList files;     // absolute paths of the files written, in listing order
    QStringList problems;  // "name in the zip: why", for drawings that could not be unpacked
    QString error;         // the zip could not be read at all
    bool cancelled = false;
};
ZipUnpacked unpackZipSubmissions(const QString& zipPath, const QString& destDir,
                                 const std::atomic_bool* cancel = nullptr,
                                 const std::function<void(int done, int total)>& progress = {});

}  // namespace dc
