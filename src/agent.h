#pragma once

#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantList>

// "Ask my agent" on the Library Doctor page. CymtX fixes what it can be sure
// of; what is left needs someone who knows the music, and on Omarchy that is
// the coding agent the person already runs. The button opens a terminal with
// that agent and one fixed request. CymtX sends nothing anywhere itself: the
// agent is another program, started the way the person would start it.
namespace AgentHelp {

struct Agent {
    QString id, name, program;
    QStringList arguments;   // the request goes after these
};

// The request, word for word. Fixed text: nothing from a song's tags or a
// file's name is put on a command line.
inline QString request() {
    return QStringLiteral(
        "Use the cymtx skill to help fix my music library's tags. Run `cymtx doctor fix` first, "
        "then `cymtx doctor list --json` and answer only what you are sure of. Show me what you "
        "plan to write before writing more than a handful, and tell me how to undo it.");
}

// The agents CymtX knows how to start with a request, in the order offered.
inline QList<Agent> known() {
    return {{"claude", "Claude Code", "claude", {}},
            {"opencode", "OpenCode", "opencode", {"--prompt"}},
            {"codex", "Codex", "codex", {}}};
}

// Those of them installed here. `paths` is where to look (tests pass a
// folder); empty means PATH.
inline QList<Agent> installed(const QStringList &paths = {}) {
    QList<Agent> found;
    for (Agent agent : known()) {
        const QString program = QStandardPaths::findExecutable(agent.program, paths);
        if (program.isEmpty())
            continue;
        agent.program = program;
        found << agent;
    }
    return found;
}

// What is run: the terminal launcher, then the agent and the request as
// separate arguments. No shell reads any of it.
inline QStringList commandLine(const Agent &agent) {
    return QStringList{agent.program} + agent.arguments + QStringList{request()};
}

}

class AgentLauncher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList agents READ agents NOTIFY agentsChanged)
    Q_PROPERTY(QString request READ request CONSTANT)

public:
    explicit AgentLauncher(QObject *parent = nullptr);
    QVariantList agents() const;
    QString request() const { return AgentHelp::request(); }
    // Looks again for installed agents (the page calls it when shown).
    Q_INVOKABLE void refresh();
    // Installs the skill if it is missing, then opens a terminal with the
    // agent and the request. False (with a message) if it could not start.
    Q_INVOKABLE bool ask(const QString &id);
    // Puts the request on the clipboard, for an agent CymtX can't start.
    Q_INVOKABLE void copyRequest();

signals:
    void agentsChanged();
    void message(const QString &text);

private:
    QList<AgentHelp::Agent> m_agents;
};
