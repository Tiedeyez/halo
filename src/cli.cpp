#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <cmath>
#include <cstdio>

#include "analysis.h"
#include "doctor.h"
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
  visual [next|previous|name]  Switch the visual: ring, bars, kaleidoscope, skyline,
                               phosphor, pulsar, waterfall, warp, aurora, halo,
                               turntable, hologram, cymatics
  sleep <minutes|off>          Fade out and pause after N minutes
  lyrics [--json]              The current song's lyrics, the line being sung marked
  lyrics online <on|off>       Look lyrics up on lrclib.net (sends title, artist,
                               album and length of each song played)
  lyrics retry                 Look again, ignoring a cached miss
  lyrics offset <+N|-N|reset>  Show this song's lyrics N seconds earlier or later
  rescan                       Look for new music

Playlists (files in ~/.local/share/halo/playlists, readable by any player):
  playlist [list]              List playlists
  playlist show <name>         The songs in a playlist
  playlist new <name> [paths]  Make a playlist, with songs from files or folders
  playlist save <name>         Save the queue as a playlist
  playlist smart <name> [any] "<field> <op> <value>" ...
                               A playlist that fills itself from rules; all must
                               match unless "any". Fields: title artist album
                               genre folder year length plays bpm loudness added
                               played. Ops: contains notContains is isNot
                               startsWith greater less inLast notInLast never
  playlist add <name> <paths>  Add songs (ones already in it are skipped)
  playlist addcurrent <name>   Add the song playing
  playlist play|shuffle|queue <name>
  playlist suggest <name> [N]  Songs that would fit, by tempo, loudness and taste
  playlist rename <name> <new> | delete <name> | restore
  playlist import <file.m3u8> | export <name> <file.m3u8>

Library Doctor (needs no running Halo; every write is logged and can be undone):
  doctor [--json]              What is wrong with the library's tags, by kind
  doctor list [kind] [--folder <dir>] [--limit N] [--json]
                               The findings, each with the file's tags, the
                               proposed fix if there is one, and how sure it is
  doctor fix [kind]            Write the fixes that are certain
  doctor set <file> <field> <value>
                               Write one tag. Fields: title artist album
                               albumartist comment tracknumber
  doctor apply <answers.json>  Write many: [{"path":..,"field":..,"value":..}],
                               undone together
  doctor undo                  Take back the last fix, set or apply, whole

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
  playing, play/pause/skip, queue songs or folders, playlists (make, add to,
  smart rules, suggestions), set volume, Blend
  (beat-aware crossfades), Drift (keeps playing similar music from the
  library), moments, the sleep timer, live lyrics, track analysis
  (tempo, loudness), and the Library Doctor (find and fix wrong or missing
  tags: artist, title, album, track numbers). Use when asked to play, queue,
  pause or skip music, what song is on, what the words are, or to clean up,
  tag, organise or fix a music library.
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
halo lyrics --json              # {"lyrics":{"status":"found","kind":"words","current":12,"lines":[..]}}
halo lyrics offset +0.3         # this song's lyrics 0.3 s earlier (saved per song)
halo playlist --json            # {"playlists":[{"id":..,"name":..,"count":..,"smart":..}]}
halo playlist new "Road Trip" ~/Music/Album   # make one (folders become their songs)
halo playlist addcurrent "Road Trip"          # add the song playing
halo playlist smart "Fast" "bpm greater 118" "played inLast 90"
halo playlist play "Road Trip" ; halo playlist suggest "Road Trip" 10
```

Playlists are .m3u8 files (smart ones .halo-smart JSON) in
~/.local/share/halo/playlists. Deleting moves the file to .trash there
(`halo playlist restore` brings the last one back).

Online lyrics (lrclib.net) is off unless the user turned it on; it sends the
title, artist, album and length of each song played. Don't switch it on
(`halo lyrics online on`) without the user asking.

## Fixing a library's tags

The Library Doctor finds wrong and missing tags and fixes what it can work
out from the tags, file names and folders. What it can't work out is left
for the person, or for you: you often know whose song a title is. It needs
no running Halo, and never touches the network.

```sh
halo doctor --json                       # {"kinds":{"noartist":{"count":127,"certain":0},..}}
halo doctor fix                          # write every certain fix (safe to run first)
halo doctor list noartist --json         # what is left, with each file's tags
halo doctor list --folder ~/Music/Tape --json
halo doctor set song.mp3 artist "Drake"  # one tag
halo doctor apply answers.json           # many, undone together
halo doctor undo                         # take back the last fix, set or apply
```

Kinds: watermark labels numbered notitle noartist noalbum nonumber spelling
album albumartist case duplicate. A finding has `path`, `field`, `before`,
`after` (the Doctor's proposal, empty when it has none), `certain`, `why`
(where the proposal comes from) and `tags` (what the file says now).
answers.json is `[{"path": "/abs/song.mp3", "field": "artist", "value": "Drake"}]`;
fields are title, artist, album, albumartist, comment, tracknumber. An empty
value clears album, albumartist or comment.

How to work:
- Run `halo doctor fix` first, then read what is left a kind or a folder at
  a time. Proposals that aren't certain say why; check them against the
  file's name, its folder and its neighbours before accepting them.
- Answer only what you know: a song you recognise, a tape whose track list
  you can find. Leave the rest for the person and say which you left. A
  wrong tag is worse than a missing one.
- Before writing more than a handful of tags, tell the person what you are
  about to change and where. Afterwards say how many tags you wrote and that
  `halo doctor undo` (or U in Halo's Library Doctor) takes them back.
- Only files in Halo's music folders can be written. Halo never deletes,
  moves or renames a music file; duplicates are only pointed out.

Music lives in ~/Music (more folders, one per line, in ~/.config/halo/folders).
Halo changes a music file only through the Library Doctor: a fix someone
pressed in the window, or a `halo doctor` command. It never moves or deletes
one. State is in ~/.local/state/halo (the Doctor's undo log is doctor.json
there) and caches (covers, analysis, library index) in ~/.cache/halo.
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
    // Paths go over as absolute paths, since Halo has its own cwd. Only for
    // the commands that take files: `halo visual halo` run from a folder that
    // holds something named "halo" must still mean the visual.
    static const QStringList takesFiles{"add", "open", "playnext", "grab"};
    for (const QString &a : args) {
        const QFileInfo info(a);
        // grab names a file that doesn't exist yet, so it is always made absolute.
        list.append(takesFiles.contains(command) && (info.exists() || command == "grab") ? info.absoluteFilePath() : a);
    }
    // `playlist`: the files after the subcommand and name, the file to
    // import, and the file to export to, go over absolute.
    if (command == "playlist" && !args.isEmpty()) {
        const QString sub = args.first();
        for (int i = 1; i < args.size(); ++i) {
            const QFileInfo info(args[i]);
            const bool file = (sub == "import" && i == 1) || (sub == "export" && i == 2) ||
                              ((sub == "new" || sub == "add") && i >= 2 && info.exists());
            if (file)
                list[i] = info.absoluteFilePath();
        }
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
    if (command == "playlist") {
        auto line = [](const QJsonObject &o) {
            return o.value("name").toString() + (o.value("smart").toBool() ? "  (smart)" : "") + "  " +
                   QString::number(o.value("count").toInt()) + (o.value("count").toInt() == 1 ? " song" : " songs");
        };
        auto printSongs = [](const QJsonArray &songs) {
            int n = 0;
            for (const auto &v : songs) {
                const QJsonObject o = v.toObject();
                out() << QString::number(++n).rightJustified(4) << "  " << o.value("title").toString()
                      << (o.value("artist").toString().isEmpty() ? QString() : " - " + o.value("artist").toString())
                      << (o.value("missing").toBool() ? "  (missing)" : "") << "\n";
            }
        };
        if (reply.contains("playlists")) {
            const QJsonArray all = reply.value("playlists").toArray();
            if (all.isEmpty())
                out() << "No playlists yet. Make one with: halo playlist new <name>\n";
            for (const auto &v : all)
                out() << (v.toObject().value("pinned").toBool() ? "* " : "  ") << line(v.toObject()) << "\n";
        } else if (reply.contains("deleted")) {
            out() << "Deleted. Bring it back with: halo playlist restore\n";
        } else if (reply.contains("file")) {
            out() << "Wrote " << reply.value("file").toString() << "\n";
        } else if (reply.contains("added")) {
            out() << "Added " << reply.value("added").toInt();
            if (reply.value("skipped").toInt() > 0)
                out() << " (" << reply.value("skipped").toInt() << " already in it)";
            out() << ". " << line(reply.value("playlist").toObject()) << "\n";
        } else if (reply.contains("songs") && !reply.contains("playlist")) {
            printSongs(reply.value("songs").toArray());
        } else if (reply.contains("playlist")) {
            out() << line(reply.value("playlist").toObject()) << "\n";
            printSongs(reply.value("songs").toArray());
        } else if (reply.contains("status")) {
            printStatus(reply.value("status").toObject());
        }
        return 0;
    }
    if (command == "visual") {
        out() << "Visual: " << reply.value("visual").toString() << "\n";
        return 0;
    }
    if (command == "lyrics") {
        const QJsonObject l = reply.value("lyrics").toObject();
        const QString status = l.value("status").toString();
        const QString kind = l.value("kind").toString();
        if (status == "offline") {
            out() << "No lyrics on this computer for this song. Turn on lookups with: halo lyrics online on\n";
            return 0;
        }
        if (status != "found") {
            out() << (status == "searching" ? "Looking for lyrics..." : status == "not-found" ? "No lyrics found."
                      : status == "failed" ? "Couldn't reach lrclib.net." : "Nothing playing.") << "\n";
            return 0;
        }
        if (kind == "instrumental") {
            out() << "Instrumental.\n";
            return 0;
        }
        const int current = l.value("current").toInt();
        const QJsonArray lines = l.value("lines").toArray();
        for (int i = 0; i < lines.size(); ++i) {
            const QJsonObject line = lines[i].toObject();
            QString stamp = "     ";
            if (line.contains("start")) {
                const int s = int(line.value("start").toDouble());
                stamp = QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
            }
            out() << (i == current ? "> " : "  ") << stamp.rightJustified(6) << "  " << line.value("text").toString() << "\n";
        }
        out() << "\n" << (kind == "words" ? "Timed word by word" : kind == "synced" ? "Timed line by line" : "Not timed")
              << " · from " << l.value("source").toString() << "\n";
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

// `halo doctor ...`: the Library Doctor without a window, for the terminal
// and for agents. The same checks, the same guarded writes and the same
// undo log as the Doctor page.
int doctor(QStringList args, bool json) {
    const QString sub = args.isEmpty() ? QString("summary") : args.takeFirst();
    QString folder;
    int limit = -1;
    for (int i = 0; i < args.size();) {
        if ((args[i] == "--folder" || args[i] == "--limit") && i + 1 < args.size()) {
            if (args[i] == "--folder")
                folder = QDir::cleanPath(QFileInfo(args[i + 1]).absoluteFilePath());
            else
                limit = std::max(0, args[i + 1].toInt());
            args.remove(i, 2);
        } else {
            ++i;
        }
    }
    static const QStringList kinds{"watermark", "labels", "numbered", "notitle", "noartist", "noalbum", "nonumber",
                                   "spelling", "album", "albumartist", "case", "duplicate"};
    static const QStringList subs{"summary", "list", "fix", "set", "apply", "undo"};
    const char *usage = "usage: halo doctor [list [kind] [--folder <dir>] [--limit N] | fix [kind] | "
                        "set <file> <field> <value> | apply <answers.json> | undo] [--json]";
    if (!subs.contains(sub) || (sub == "set" && args.size() != 3) || (sub == "apply" && args.size() != 1)
        || ((sub == "list" || sub == "fix") && args.size() > 1) || ((sub == "summary" || sub == "undo") && !args.isEmpty()))
        return fail(usage, json);
    if ((sub == "list" || sub == "fix") && !args.isEmpty() && !kinds.contains(args.first()))
        return fail(QString("%1 isn't a kind of finding. Kinds: %2").arg(args.first(), kinds.join(" ")), json);

    LibraryDoctor doctor(nullptr);
    QStringList said;
    QObject::connect(&doctor, &LibraryDoctor::message, [&said](const QString &text) { said << text; });
    auto done = [&](int count, const QString &verb) {
        // A running Halo shows the new tags once it looks again.
        if (count > 0 && Remote::running(300))
            Remote::send({{"cmd", "rescan"}, {"args", QJsonArray()}}, nullptr, 1000);
        if (json)
            out() << QJsonDocument(QJsonObject{{"ok", true}, {verb, count}, {"said", QJsonArray::fromStringList(said)},
                                               {"undoable", doctor.undoable()}})
                         .toJson(QJsonDocument::Compact)
                  << "\n";
        else
            for (const QString &line : std::as_const(said))
                out() << line << "\n";
        return 0;
    };
    if (sub == "undo") {
        const int before = doctor.undoable();
        if (before == 0)
            return fail("Nothing to undo.", json);
        const bool whole = doctor.undoLast();
        const int count = before - doctor.undoable();
        if (!json && said.isEmpty())
            said << QString("Undid %1 fix%2.").arg(count).arg(count == 1 ? "" : "es");
        done(count, "undone");
        return whole ? 0 : 1;
    }

    // The library as the window would show it.
    const QStringList folders = Library::defaultFolders();
    QStringList paths;
    for (const Track &t : Library::scanFolders(folders, {}))
        if (t.duration <= 0 || t.duration >= Library::MinimumSeconds)
            paths << t.path;
    const QSet<QString> inLibrary(paths.cbegin(), paths.cend());

    if (sub == "set" || sub == "apply") {
        QList<LibraryDoctor::Answer> answers;
        if (sub == "set") {
            answers.append({QFileInfo(args[0]).absoluteFilePath(), args[1].toLower(), args[2]});
        } else {
            // The answers file: read once, and no further than this.
            constexpr qint64 MaxAnswers = 8 * 1024 * 1024;
            QFile file(args[0]);
            if (!file.open(QIODevice::ReadOnly))
                return fail(QString("%1: %2").arg(args[0], file.errorString()), json);
            const QByteArray bytes = file.read(MaxAnswers + 1);
            if (bytes.size() > MaxAnswers)
                return fail(QString("%1 is larger than 8 MiB; send it in parts").arg(args[0]), json);
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
            if (!doc.isArray())
                return fail(QString("%1 isn't a JSON list of {\"path\", \"field\", \"value\"}%2")
                                .arg(args[0], error.error == QJsonParseError::NoError ? QString() : ": " + error.errorString()),
                            json);
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                if (!o.value("path").isString() || !o.value("field").isString() || !o.value("value").isString())
                    return fail("every answer needs \"path\", \"field\" and \"value\" as text; nothing was written", json);
                answers.append({QFileInfo(o.value("path").toString()).absoluteFilePath(),
                                o.value("field").toString().toLower(), o.value("value").toString()});
            }
        }
        // Only the library's own songs, named as the library names them.
        QList<LibraryDoctor::Answer> inside;
        for (const LibraryDoctor::Answer &a : std::as_const(answers)) {
            if (inLibrary.contains(a.path))
                inside.append(a);
            else
                said << QString("%1 isn't a song in Halo's music folders; left alone.").arg(a.path);
        }
        const int count = doctor.applyAnswers(inside);
        done(count, "written");
        return count == answers.size() ? 0 : 1;
    }

    doctor.diagnoseFiles(paths, folders);
    if (sub == "fix")
        return done(doctor.fixCertain(args.value(0)), "fixed");

    const std::vector<Doctor::Finding> &found = doctor.found();
    if (sub == "summary") {
        QJsonObject byKind;
        int total = 0, certain = 0, open = 0;
        for (const QString &kind : kinds) {
            int count = 0, sure = 0, proposed = 0;
            for (const Doctor::Finding &f : found)
                if (Doctor::kindName(f.kind) == kind) {
                    ++count;
                    sure += f.sure;
                    proposed += !f.field.isEmpty() && !f.sure;
                }
            if (count == 0)
                continue;
            byKind.insert(kind, QJsonObject{{"count", count}, {"certain", sure}, {"toCheck", proposed},
                                            {"noProposal", count - sure - proposed}});
            total += count, certain += sure, open += count - sure - proposed;
            if (!json)
                out() << kind.leftJustified(12) << QString::number(count).rightJustified(6) << "   " << sure
                      << " certain, " << proposed << " to check, " << (count - sure - proposed) << " without a proposal\n";
        }
        if (json)
            out() << QJsonDocument(QJsonObject{{"ok", true}, {"songs", int(paths.size())}, {"findings", total},
                                               {"certain", certain}, {"kinds", byKind}, {"undoable", doctor.undoable()}})
                         .toJson(QJsonDocument::Compact)
                  << "\n";
        else
            out() << total << " findings in " << paths.size() << " songs; " << certain
                  << " certain (halo doctor fix writes those), " << open << " with no proposal.\n";
        return 0;
    }

    // list
    QJsonArray list;
    int shown = 0, matching = 0;
    for (const Doctor::Finding &f : found) {
        if (!args.isEmpty() && Doctor::kindName(f.kind) != args.first())
            continue;
        if (!folder.isEmpty() && QFileInfo(f.path).absolutePath() != folder && !f.path.startsWith(folder + "/"))
            continue;
        ++matching;
        if (limit >= 0 && shown >= limit)
            continue;
        ++shown;
        if (json) {
            Doctor::Tags t;
            Doctor::readTags(f.path, t);
            QJsonObject o{{"kind", Doctor::kindName(f.kind)}, {"path", f.path}, {"field", f.field},
                          {"before", f.before}, {"after", f.after}, {"certain", f.sure}, {"why", f.why},
                          {"tags", QJsonObject{{"title", t.title}, {"artist", t.artist}, {"album", t.album},
                                               {"albumartist", t.albumArtist}, {"tracknumber", t.number},
                                               {"seconds", int(t.duration)}}}};
            if (!f.others.isEmpty())
                o.insert("others", QJsonArray::fromStringList(f.others));
            list.append(o);
        } else {
            out() << Doctor::kindName(f.kind).leftJustified(12) << f.path << "\n";
            if (!f.field.isEmpty())
                out() << "            " << f.field << ": " << (f.before.isEmpty() ? "(empty)" : f.before) << " -> "
                      << (f.after.isEmpty() ? "(empty)" : f.after) << (f.sure ? "" : "   (to check: " + f.why + ")") << "\n";
            for (const QString &other : f.others)
                out() << "            also " << other << "\n";
        }
    }
    if (json)
        out() << QJsonDocument(QJsonObject{{"ok", true}, {"count", matching}, {"shown", shown}, {"findings", list}})
                     .toJson(QJsonDocument::Compact)
              << "\n";
    else if (shown < matching)
        out() << shown << " of " << matching << " shown.\n";
    return 0;
}

// Installs like Hype: the skill lives in ~/.agents/skills/halo and is linked
// into ~/.claude/skills so every agent on the machine finds it.
int skill(const QStringList &args) {
    if (args.isEmpty()) {
        out() << Skill;
        return 0;
    }
    const bool force = args == QStringList{"install", "--force"};
    if (args != QStringList{"install"} && !force)
        return fail("usage: halo skill [install [--force]]", false);
    const QString home = QDir::homePath();
    const QString directory = home + "/.agents/skills/halo";
    // Only replace a SKILL.md this command wrote (it starts with "name: halo");
    // anything else there is someone else's, so it's left alone without --force.
    {
        QFile existing(directory + "/SKILL.md");
        if (!force && existing.open(QIODevice::ReadOnly)) {
            const QByteArray head = existing.read(256);
            if (!head.startsWith("---\nname: halo\n"))
                return fail(QString("%1 isn't Halo's; left it alone (halo skill install --force replaces it)")
                                .arg(existing.fileName()), false);
        }
    }
    if (QFileInfo(directory).isSymLink())
        return fail(QString("%1 is a link; left it alone").arg(directory), false);
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
                                      "shuffle", "normalize", "repeat", "sleep", "rescan", "scan", "analyze", "lyrics", "visual", "playlist",
                                      "doctor", "skill", "grab", "ui", "help", "-h", "--help", "-v", "--version"};
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
    if (command == "doctor")
        return doctor(args, json);
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
