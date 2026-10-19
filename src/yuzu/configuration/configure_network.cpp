// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QtConcurrent/QtConcurrent>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/acc/switchnet_account.h"
#include "core/hle/service/friend/grid0_friends.h"
#include "core/internal_network/network_interface.h"
#include "qt_common/config/uisettings.h"
#include "ui_configure_network.h"
#include "yuzu/configuration/configure_network.h"
#include "yuzu/grid0_friends_dialog.h"

namespace {
auto Grid0Settings() {
    return std::array{&Settings::values.switchnet_username, &Settings::values.switchnet_password,
                      &Settings::values.switchnet_server, &Settings::values.private_server_address,
                      &Settings::values.private_server_nat_secondary_address,
                      &Settings::values.private_server_ca_bundle};
}
} // namespace

ConfigureNetwork::ConfigureNetwork(const Core::System& system_, QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureNetwork>()), system{system_} {
    ui->setupUi(this);
    ui->network_interface->addItem(tr("Automatic"), QString{});
    ui->network_interface->addItem(tr("None"), QStringLiteral("None"));
    for (const auto& iface : Network::GetAvailableNetworkInterfaces())
        ui->network_interface->addItem(QString::fromStdString(iface.name),
                                       QString::fromStdString(iface.name));

    auto* group = new QGroupBox(tr("GRID0+"), this);
    auto* form = new QFormLayout(group);
    const std::array labels{tr("Username"), tr("Password"), tr("Login server"),
                            tr("Game server address"), tr("Secondary NAT address"),
                            tr("Custom CA bundle (PEM path)")};
    for (size_t index = 0; index < grid0_fields.size(); ++index) {
        grid0_fields[index] = new QLineEdit(group);
        grid0_fields[index]->setEnabled(!system.IsPoweredOn());
        form->addRow(labels[index], grid0_fields[index]);
    }
    grid0_fields[1]->setEchoMode(QLineEdit::Password);
    login_button = new QPushButton(tr("Apply and test GRID0+ login"), group);
    login_button->setEnabled(!system.IsPoweredOn());
    form->addRow(login_button);
    connect(login_button, &QPushButton::clicked, this, [this] {
        ApplyConfiguration();
        UISettings::values.configuration_applied = true;
        login_button->setEnabled(false);
        auto* watcher = new QFutureWatcher<Service::Account::SwitchNet::LoginCheck>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
            const auto result = watcher->result();
            login_button->setEnabled(!system.IsPoweredOn());
            QMessageBox::information(this, tr("GRID0+ login"),
                                     QString::fromStdString(result.message));
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([] { return Service::Account::SwitchNet::CheckLogin(); }));
    });
    auto* friends = new QPushButton(tr("Apply and open GRID0+ Friends"), group);
    form->addRow(friends);
    connect(friends, &QPushButton::clicked, this, [this] {
        ApplyConfiguration();
        UISettings::values.configuration_applied = true;
        auto* dialog = new Grid0FriendsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    ui->verticalLayout->insertWidget(1, group);
    this->SetConfiguration();
}

ConfigureNetwork::~ConfigureNetwork() = default;

void ConfigureNetwork::ApplyConfiguration() {
    Settings::values.network_interface = ui->network_interface->currentData().toString().toStdString();
    Settings::values.airplane_mode = ui->airplane_mode->isChecked();
    if (!system.IsPoweredOn()) {
        const auto settings = Grid0Settings();
        for (size_t index = 0; index < settings.size(); ++index) {
            const auto value = index == 1 ? grid0_fields[index]->text()
                                          : grid0_fields[index]->text().trimmed();
            settings[index]->SetValue(value.toStdString());
        }
        Service::Friend::Grid0::Invalidate();
    }
}

void ConfigureNetwork::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureNetwork::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureNetwork::SetConfiguration() {
    const bool runtime_lock = !system.IsPoweredOn();
    auto const network_interface = Settings::values.network_interface.GetValue();
    auto const airplane_mode = Settings::values.airplane_mode.GetValue();
    const auto name = QString::fromStdString(network_interface);
    if (ui->network_interface->findData(name) < 0) {
        ui->network_interface->addItem(name, name);
    }
    ui->network_interface->setCurrentIndex(ui->network_interface->findData(name));
    ui->network_interface->setEnabled(runtime_lock);
    ui->airplane_mode->setChecked(airplane_mode);
    const auto settings = Grid0Settings();
    for (size_t index = 0; index < settings.size(); ++index) {
        grid0_fields[index]->setText(QString::fromStdString(settings[index]->GetValue()));
    }
}
