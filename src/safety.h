#pragma once

// Small, dependency-free guards shared by the app and the tests:
//   - system tools are taken from the system folders before PATH, so an
//     executable dropped in ~/.local/bin can't stand in for ffmpeg;
//   - everything CymtX writes is private to you (folders 0700, files 0600),
//     and files an older build left readable are corrected once at startup;
//   - online lyrics only ever talk to https (or a loopback test server).

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Safety {

// An absolute path to a system tool: /usr/bin and /bin first, PATH last.
inline QString systemTool(const QString &name) {
    const QString system = QStandardPaths::findExecutable(name, {"/usr/bin", "/bin", "/usr/local/bin"});
    return system.isEmpty() ? QStandardPaths::findExecutable(name) : system;
}

namespace detail {
// Tightens one folder tree that belongs to CymtX. Only your own regular files
// and folders are changed; symlinks are never followed. Bounded, so a huge
// cover cache can't stall startup.
inline void tighten(int dirfd, int depth, int &budget) {
    struct stat st {};
    if (fstat(dirfd, &st) != 0 || st.st_uid != getuid() || !S_ISDIR(st.st_mode))
        return;
    if ((st.st_mode & 077) != 0)
        fchmod(dirfd, 0700);
    if (depth > 4)
        return;
    const int copy = dup(dirfd);
    if (copy < 0)
        return;
    DIR *dir = fdopendir(copy);
    if (!dir) {
        close(copy);
        return;
    }
    while (dirent *e = readdir(dir)) {
        if (--budget <= 0)
            break;
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
            continue;
        const int fd = openat(dirfd, e->d_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            continue;   // a symlink (ELOOP) or something we can't open: left alone
        struct stat est {};
        if (fstat(fd, &est) == 0 && est.st_uid == getuid()) {
            if (S_ISDIR(est.st_mode))
                tighten(fd, depth + 1, budget);
            else if (S_ISREG(est.st_mode) && (est.st_mode & 077) != 0)
                fchmod(fd, 0600);
        }
        close(fd);
    }
    closedir(dir);
}
}

// Called first thing in main(): new files are 0600 and folders 0700, and
// CymtX's own folders from older builds are corrected.
inline void makePrivate(const QStringList &folders) {
    umask(077);
    int budget = 50000;
    for (const QString &folder : folders) {
        const QByteArray path = QFile::encodeName(folder);
        const int fd = open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            continue;
        detail::tighten(fd, 0, budget);
        close(fd);
    }
}

inline QStringList ownFolders() {
    return {QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/cymtx",
            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/cymtx",
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/cymtx",
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/cymtx"};
}

// CymtX was called Halo until 1.1.0. The first time it starts where a Halo
// has been, it takes a copy of each of Halo's folders (settings and queue,
// playlists, the analysis and cover caches, the music folders) under its own
// name, so nothing has to be set up or analysed again. A copy, not a move:
// the old folders are left exactly as they were, and a CymtX folder that
// already exists is never touched. Links are not followed or copied. Smart
// playlists take the new suffix. `from` and `to` are the two folder names
// (tests use others); returns how many folders were adopted.
inline int adoptOldFolders(const QStringList &roots, const QString &from = "halo", const QString &to = "cymtx") {
    int adopted = 0;
    for (const QString &root : roots) {
        const QString source = root + "/" + from, target = root + "/" + to;
        if (root.isEmpty() || QFileInfo::exists(target) || QFileInfo(source).isSymLink() || !QFileInfo(source).isDir())
            continue;
        // Built beside the target and renamed into place, so a copy cut
        // short never leaves a half-filled folder that looks finished.
        const QString partial = target + ".adopting";
        QDir(partial).removeRecursively();
        bool ok = QDir().mkpath(partial);
        QDirIterator walk(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
        while (ok && walk.hasNext()) {
            const QFileInfo entry(walk.next());
            if (entry.isSymLink())
                continue;
            QString relative = entry.absoluteFilePath().mid(source.size());
            if (relative.endsWith("." + from + "-smart"))
                relative = relative.left(relative.size() - (from.size() + 6)) + to + "-smart";
            if (entry.isDir())
                ok = QDir().mkpath(partial + relative);
            else if (entry.isFile())
                ok = QDir().mkpath(QFileInfo(partial + relative).absolutePath()) && QFile::copy(entry.absoluteFilePath(), partial + relative);
        }
        if (ok && QDir().rename(partial, target))
            ++adopted;
        else
            QDir(partial).removeRecursively();
    }
    return adopted;
}

inline QStringList ownRoots() {
    return {QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)};
}

// The lyrics endpoint: https anywhere, plain http only to this machine.
inline bool allowedEndpoint(const QString &endpoint) {
    const QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty())
        return false;
    if (url.scheme() == "https")
        return true;
    if (url.scheme() != "http")
        return false;
    const QHostAddress address(url.host());
    return url.host() == "localhost" || (!address.isNull() && address.isLoopback());
}

// Bounds on what a peer can make CymtX hold in memory.
constexpr qint64 MaxSocketLine = 64 * 1024;      // one control-socket command
constexpr qint64 MaxLyricsReply = 2 * 1024 * 1024;   // one LRCLIB response
// One reply a `cymtx` command reads back from the window. A whole queue or a
// big playlist is a few MiB at most; more than this is not CymtX talking.
constexpr qint64 MaxSocketReply = 16 * 1024 * 1024;

}
