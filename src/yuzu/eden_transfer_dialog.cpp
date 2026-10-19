// SPDX-FileCopyrightText: 2026 GRID0
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yuzu/eden_transfer_dialog.h"
#include "yuzu/eden_transfer.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

EdenTransferWizard::EdenTransferWizard(const QString& data, const QString& config, QWidget* parent)
    : QWizard(parent) {
    setWindowTitle(tr("Transfer from Eden"));
    setMinimumSize(620, 380);
    setOption(QWizard::NoBackButtonOnStartPage);
    auto* source_page = new QWizardPage;
    source_page->setTitle(tr("Choose your Eden user folder"));
    auto* layout = new QVBoxLayout(source_page);
    auto* description = new QLabel(tr("Close Eden before transferring. Detected folders are listed below; you can also browse to its user or configuration folder."));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* detected = new QComboBox;
    const auto sources = EdenTransfer::Detect();
    for (const auto& source : sources) detected->addItem(source.data, source.config);
    layout->addWidget(detected);
    auto* path = new QLineEdit;
    path->setPlaceholderText(tr("Eden user folder"));
    if (!sources.isEmpty()) path->setText(sources.front().data);
    layout->addWidget(path);
    auto* browse = new QPushButton(tr("Browse…"));
    layout->addWidget(browse);
    connect(detected, &QComboBox::currentIndexChanged, this, [=](int index) { path->setText(detected->itemText(index)); });
    connect(browse, &QPushButton::clicked, this, [=, this] {
        const auto selected = QFileDialog::getExistingDirectory(this, tr("Eden user folder"), path->text());
        if (!selected.isEmpty()) path->setText(selected);
    });
    addPage(source_page);
    auto* mode_page = new QWizardPage;
    mode_page->setTitle(tr("Choose what to transfer"));
    auto* modes = new QVBoxLayout(mode_page);
    auto* full = new QRadioButton(tr("Full transfer"));
    auto* settings = new QRadioButton(tr("Configs and paths only"));
    full->setChecked(true);
    modes->addWidget(full);
    auto* full_text = new QLabel(tr("Copy settings, keys, firmware, saves, save states, mods, shaders, play time, amiibo, TAS files, dumps and screenshots into Citrosis. Games stay at their current locations. Shader caches are not transferred."));
    full_text->setWordWrap(true);
    modes->addWidget(full_text);
    modes->addWidget(settings);
    auto* config_text = new QLabel(tr("Copy global and per-game configurations and game-library paths. Data paths keep pointing to Eden's existing files, including saves and installed content. Keys are included only in Full transfer."));
    config_text->setWordWrap(true);
    modes->addWidget(config_text);
    auto* summary = new QLabel(tr("Destination: %1\n\nReplaced files are backed up under transfer-backups in this folder. Eden's files are kept. Citrosis restarts after a successful transfer.").arg(data));
    summary->setWordWrap(true);
    modes->addWidget(summary);
    addPage(mode_page);
    setButtonText(QWizard::FinishButton, tr("Transfer"));
    transfer = [=, this] {
        EdenTransfer::Source source = EdenTransfer::FromDirectory(path->text());
        for (const auto& candidate : sources)
            if (candidate.data == path->text()) source = candidate;
        if (path->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Transfer from Eden"), tr("Choose an Eden folder first."));
            return;
        }
        std::atomic_bool cancelled{false};
        QProgressDialog progress(tr("Copying Eden data…"), tr("Cancel"), 0, 0, this);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        QFutureWatcher<EdenTransfer::Result> watcher;
        QEventLoop loop;
        bool running = true;
        connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        connect(&progress, &QProgressDialog::canceled, &loop, [&] {
            cancelled.store(true);
            progress.setLabelText(tr("Cancelling and restoring files…"));
            progress.setCancelButton(nullptr);
            QTimer::singleShot(0, &progress, [&] { if (running) progress.show(); });
        });
        const bool copy_full = full->isChecked();
        watcher.setFuture(QtConcurrent::run([=, &cancelled] { return EdenTransfer::Copy(source, data, config, copy_full, &cancelled); }));
        progress.show();
        if (!watcher.isFinished()) loop.exec();
        running = false;
        QObject::disconnect(&progress, nullptr, &loop, nullptr);
        progress.close();
        const auto result = watcher.result();
        if (!result.ok) {
            if (cancelled.load()) QMessageBox::information(this, tr("Transfer cancelled"), result.error);
            else QMessageBox::critical(this, tr("Transfer failed"), result.error);
            return;
        }
        QMessageBox::information(this, tr("Transfer complete"), tr("Transferred %1 files.\nBackup location: %2\n\nCitrosis will restart now.").arg(result.files).arg(result.backup));
        done(QDialog::Accepted);
    };
}
