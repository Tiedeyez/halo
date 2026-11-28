#include "agent.h"

#include "cli.h"
#include "safety.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>

AgentLauncher::AgentLauncher(QObject *parent) : QObject(parent), m_agents(AgentHelp::installed()) {}

QVariantList AgentLauncher::agents() const {
    QVariantList out;
    for (const AgentHelp::Agent &agent : m_agents)
        out << QVariantMap{{"id", agent.id}, {"name", agent.name}};
    return out;
}

void AgentLauncher::refresh() {
    const QList<AgentHelp::Agent> now = AgentHelp::installed();
    bool same = now.size() == m_agents.size();
    for (int i = 0; same && i < now.size(); ++i)
        same = now[i].program == m_agents[i].program;
    if (same)
        return;
    m_agents = now;
    emit agentsChanged();
}

bool AgentLauncher::ask(const QString &id) {
    for (const AgentHelp::Agent &agent : m_agents) {
        if (agent.id != id)
            continue;
        const QString terminal = Safety::systemTool("xdg-terminal-exec");
        if (terminal.isEmpty()) {
            copyRequest();
            emit message(tr("Couldn't find a terminal to open. The request is on your clipboard: paste it into %1.").arg(agent.name));
            return false;
        }
        QString error;
        if (!Cli::installSkill(&error)) {
            emit message(tr("Couldn't install the CymtX skill: %1").arg(error));
            return false;
        }
        if (!QProcess::startDetached(terminal, AgentHelp::commandLine(agent), QDir::homePath())) {
            emit message(tr("Couldn't start %1.").arg(agent.name));
            return false;
        }
        emit message(tr("Opened %1 in a terminal. Check again here when it has finished.").arg(agent.name));
        return true;
    }
    return false;
}

void AgentLauncher::copyRequest() {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(AgentHelp::request());
    emit message(tr("Copied. Run cymtx skill install once, then paste this into your agent."));
}
