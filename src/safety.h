#pragma once

// Small, dependency-free guards shared by the app and the tests:
//   - system tools are taken from the system folders before PATH, so an
//     executable dropped in ~/.local/bin can't stand in for ffmpeg;
//   - everything Halo writes is private to you (folders 0700, files 0600),
//     and files an older build left readable are corrected once at startup;
//   - online lyrics only ever talk to https (or a loopback test server).

#include <QFile>
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
// Tightens one folder tree that belongs to Halo. Only your own regular files
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
// Halo's own folders from older builds are corrected.
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
    return {QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/halo",
            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/halo",
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/halo"};
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

// Bounds on what a peer can make Halo hold in memory.
constexpr qint64 MaxSocketLine = 64 * 1024;      // one control-socket command
constexpr qint64 MaxLyricsReply = 2 * 1024 * 1024;   // one LRCLIB response

}
