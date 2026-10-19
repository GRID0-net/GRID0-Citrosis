// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWizard>
#include <functional>
class EdenTransferWizard final : public QWizard {
public:
    EdenTransferWizard(const QString& data, const QString& config, QWidget* parent);
private:
    void accept() override { transfer(); }
    std::function<void()> transfer;
};
