// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QList>
#include <atomic>
namespace EdenTransfer {
struct Source { QString data; QString config; };
struct Result { bool ok = false; QString error; QString backup; int files = 0; };
QList<Source> Detect();
Source FromDirectory(const QString& directory);
Result Copy(const Source& source, const QString& data, const QString& config, bool full, const std::atomic_bool* cancelled = nullptr);
}
