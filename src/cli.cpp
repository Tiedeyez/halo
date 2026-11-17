#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <cstdio>

#include "analysis.h"
#include "library.h"
#include "remote.h"

namespace {

const char *Usage = R"(Usage: halo [open] [file or folder ...]
A music player for Omarchy: your library, mixed like a DJ would.

Window:
  open [paths]                 Open Halo, playing the files or folders given

Control a running Halo:
  status [--json]              What is playing
  play | pause | toggle        Start, pause, or flip between them
  next | previous              Skip forward or back
  seek <seconds|+N|-N>         Jump to a time, or by N seconds
  volume <0-100>               Set the volume
  add <paths...>               Add files or folders to the end of the queue
  playnext <paths...>          Play files or folders after the current song
  queue [--json]               List the queue
  clear                        Clear the queue (the current song stays)
  moment                       Save a moment at the current second
  blend|drift|shuffle|normalize <on|off>
  repeat <off|all|one>
  sleep <minutes|off>          Fade out and pause after N minutes
  rescan                       Look for new music

Commands that need no running Halo:
  scan [--json]                Scan the music folders and report what's there
  analyze <file> [--json]      Tempo, loudness and silent edges of a track
  skill [install]              Print the skill for coding agents, or install it

Options:
  -h, --help                   Show this help
  -v, --version                Show the version
)";

const char *Skill = R"(---
name: halo
description: |
  Control Halo, the Omarchy music player, from the command line: what is
  playing, play/pause/skip, queue songs or folders, set volume, Blend
  (beat-aware crossfades), Drift (keeps playing similar music from the
  library), moments, the sleep timer, and track analysis (tempo, loudness).
  Use when asked to play, queue, pause or skip music, or what song is on.
---

# Halo

Halo must be running for control commands (`halo open` starts it). Every
command takes `--json`; it exits 0 on success and 1 with the error on stderr.

```sh
halo status --json              # {"playing":true,"title":..,"artist":..,"position":..}
halo add ~/Music/SomeAlbum      # folders are added in file-name order
halo playnext song.flac
halo open ~/Music/Album         # replace the queue and play
halo next ; halo pause ; halo seek +30 ; halo volume 60
halo queue --json
halo blend on ; halo drift on ; halo repeat all ; halo sleep 30
halo analyze track.mp3 --json   # {"bpm":..,"lufs":..,"start":..,"end":..}
halo scan --json                # library size, no window needed
```

Music lives in ~/Music (more folders, one per line, in ~/.config/halo/folders).
Halo never edits or moves music files. State is in ~/.local/state/halo and
caches (covers, analysis, library index) in ~/.cache/halo.
)";

QTextStream &out() {
    static QTextStream s(stdout);
    return s;
}

QTextStream &err() {
    static QTextStream s(stderr);
    return s;
}

int fail(const QString &message, bool json) {
    if (json)
        out() << QJsonDocument(QJsonObject{{"ok", false}, {"error", message}}).toJson(QJsonDocument::Compact) << "\n";
    err() << "halo: " << message << "\n";
    out().flush();
    err().flush();
    return 1;
}

QString clock(double seconds) {
    const int s = int(std::max(0.0, seconds));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

void printStatus(const QJsonObject &s) {
    if (s.value("path").toString().isEmpty()) {
        out() << "Nothing is playing.\n";
        return;
    }
    out() << (s.value("playing").toBool() ? "Playing  " : "Paused   ") << s.value("title").toString();
    if (!s.value("artist").toString().isEmpty())
        out() << " - " << s.value("artist").toString();
    out() << "\n         " << clock(s.value("position").toDouble()) << " / " << clock(s.value("duration").toDouble())
          << "   volume " << s.value("volume").toInt() << "%";
    const QJsonObject a = s.value("analysis").toObject();
    if (a.value("bpm").toDouble() > 0)
        out() << "   " << a.value("bpm").toDouble() << " BPM";
    out() << "\n";
    const QJsonObject next = s.value("next").toObject();
    if (!next.isEmpty())
        out() << "Next     " << next.value("title").toString()
              << (next.value("artist").toString().isEmpty() ? QString() : " - " + next.value("artist").toString())
              << "\n";
}

int remote(const QString &command, const QStringList &args, bool json) {
    QJsonArray list;
    for (const QString &a : args) {
        // Paths go over as absolute paths, since Halo has its own cwd.
        const QFileInfo info(a);
        list.append(info.exists() ? info.absoluteFilePath() : a);
    }
    QJsonObject reply;
    if (!Remote::send({{"cmd", command}, {"args", list}}, &reply, 3000))
        return fail("Halo isn't running. Start it with `halo open`.", json);
    if (!reply.value("ok").toBool())
        return fail(reply.value("error").toString("the command failed"), json);
    if (json) {
        out() << QJsonDocument(reply).toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }
    if (command == "queue") {
        for (const auto &v : reply.value("queue").toArray()) {
            const QJsonObject o = v.toObject();
            out() << (o.value("current").toBool() ? "> " : "  ") << QString::number(o.value("index").toInt() + 1).rightJustified(3)
                  << "  " << o.value("title").toString()
                  << (o.value("artist").toString().isEmpty() ? QString() : " - " + o.value("artist").toString())
                  << (o.value("drift").toBool() ? "  (drift)" : "") << "\n";
        }
        return 0;
    }
    if (reply.contains("status"))
        printStatus(reply.value("status").toObject());
    return 0;
}

int scan(bool json) {
    const QStringList folders = Library::defaultFolders();
    std::vector<Track> tracks;
    for (Track &t : Library::scanFolders(folders, {}))
        if (t.duration <= 0 || t.duration >= Library::MinimumSeconds)
            tracks.push_back(std::move(t));
    const std::vector<Album> albums = Library::buildAlbums(tracks);
    double total = 0;
    for (const Track &t : tracks)
        total += t.duration;
    if (json) {
        out() << QJsonDocument(QJsonObject{{"ok", true},
                                           {"folders", QJsonArray::fromStringList(folders)},
                                           {"tracks", int(tracks.size())},
                                           {"albums", int(albums.size())},
                                           {"hours", std::round(total / 360) / 10}})
                     .toJson(QJsonDocument::Compact)
              << "\n";
        return 0;
    }
    out() << tracks.size() << " tracks in " << albums.size() << " albums, " << std::round(total / 360) / 10
          << " hours, in " << folders.join(", ") << "\n";
    return 0;
}

int analyze(const QStringList &args, bool json) {
    if (args.size() != 1)
        return fail("usage: halo analyze <file> [--json]", json);
    if (!QFileInfo(args.first()).isFile())
        return fail(QString("%1: no such file").arg(args.first()), json);
    QString error;
    const TrackAnalysis a = Analysis::analyzeFile(args.first(), &error);
    if (!a.valid)
        return fail(QString("%1: %2").arg(args.first(), error), json);
    if (json) {
        QJsonObject o = a.toJson();
        o.insert("ok", true);
        out() << QJsonDocument(o).toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }
    out() << "Tempo     " << (a.bpm > 0 ? QString("%1 BPM").arg(a.bpm) : QString("no steady beat")) << "\n"
          << "Loudness  " << QString::number(a.loudness, 'f', 1) << " LUFS\n"
          << "Music     " << clock(a.audibleStart) << " to " << clock(a.audibleEnd) << "\n";
    return 0;
}

// Installs like Hype: the skill lives in ~/.agents/skills/halo and is linked
// into ~/.claude/skills so every agent on the machine finds it.
int skill(const QStringList &args) {
    if (args.isEmpty()) {
        out() << Skill;
        return 0;
    }
    if (args != QStringList{"install"})
        return fail("usage: halo skill [install]", false);
    const QString home = QDir::homePath();
    const QString directory = home + "/.agents/skills/halo";
    QDir().mkpath(directory);
    QSaveFile file(directory + "/SKILL.md");
    const QByteArray bytes(Skill);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QString("%1: %2").arg(file.fileName(), file.errorString()), false);
    const QString link = home + "/.claude/skills/halo";
    QDir().mkpath(home + "/.claude/skills");
    const QFileInfo existing(link);
    if (!existing.exists() && !existing.isSymLink())
        QFile::link("../../.agents/skills/halo", link);
    else if (!existing.isSymLink())
        err() << "halo: left " << link << " alone; it isn't a link this command made\n";
    out() << "Installed the Halo skill in " << directory << "\n";
    return 0;
}

}

namespace Cli {

bool isCommand(const QStringList &arguments) {
    static const QStringList commands{"status", "play", "pause", "toggle", "next", "previous", "prev", "seek",
                                      "volume", "add", "playnext", "queue", "clear", "moment", "blend", "drift",
                                      "shuffle", "normalize", "repeat", "sleep", "rescan", "scan", "analyze",
                                      "skill", "grab", "ui", "help", "-h", "--help", "-v", "--version"};
    return arguments.size() > 1 && commands.contains(arguments[1]);
}

int run(const QStringList &arguments) {
    QStringList args = arguments.mid(1);
    QString command = args.takeFirst();
    const bool json = args.removeAll("--json") > 0;

    if (command == "-h" || command == "--help" || command == "help") {
        out() << Usage;
        return 0;
    }
    if (command == "-v" || command == "--version") {
        out() << "halo " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }
    if (command == "scan")
        return scan(json);
    if (command == "analyze")
        return analyze(args, json);
    if (command == "skill")
        return skill(args);
    if (command == "prev")
        command = "previous";
    static const QStringList needArgument{"seek", "volume", "add", "playnext", "blend", "drift", "shuffle",
                                          "normalize", "repeat", "sleep"};
    if (needArgument.contains(command) && args.isEmpty())
        return fail(QString("%1 needs a value; see halo --help").arg(command), json);
    return remote(command, args, json);
}

}
