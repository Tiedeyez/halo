#pragma once

#include <QString>
#include <QStringList>

// Commands that need no display: they talk to a running CymtX over its local
// socket, or (scan, analyze) do their work headless.
namespace Cli {
bool isCommand(const QStringList &arguments);
int run(const QStringList &arguments);
// Writes the agent skill to ~/.agents/skills/cymtx and links it into
// ~/.claude/skills. Leaves a skill there that isn't CymtX's alone unless
// `force`. False, with why in `error`, if nothing was installed.
bool installSkill(QString *error = nullptr, bool force = false);
}
