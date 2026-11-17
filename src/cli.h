#pragma once

#include <QStringList>

// Commands that need no display: they talk to a running Halo over its local
// socket, or (scan, analyze) do their work headless.
namespace Cli {
bool isCommand(const QStringList &arguments);
int run(const QStringList &arguments);
}
