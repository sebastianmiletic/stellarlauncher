// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "MSALoginDialog.h"
#include "Application.h"
#include "Handle.h"
#include "settings/SettingsObject.h"

#include "ui_MSALoginDialog.h"

#include "DesktopServices.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/AuthFlow.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QUuid>
#include <QVBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QUrl>
#include <QtWidgets/QPushButton>

#include "qrencode.h"

namespace {
QString formatError(const QString& reason)
{
    QString formatted;
    for (const auto& line : reason.split('\n')) {
        if (!line.isEmpty()) {
            formatted += "<font color='#f29292'>" + line + "</font><br />";
        } else {
            formatted += "<br />";
        }
    }
    return formatted;
}
bool configureMicrosoftApplication(QWidget* parent)
{
    if (APPLICATION->capabilities().testFlag(Application::SupportsMSA))
        return true;

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Set Up Microsoft Sign-In"));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* help = new QLabel(QObject::tr("Mojang must approve Stellar’s Microsoft application before it can sign in to Minecraft. "
                                       "Enter the approved application ID to continue."), &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);

    auto* id = new QLineEdit(&dialog);
    id->setAccessibleName(QObject::tr("Approved Microsoft application ID"));
    id->setPlaceholderText(QObject::tr("Application (client) ID"));
    id->setClearButtonEnabled(true);
    layout->addWidget(id);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* learn = buttons->addButton(QObject::tr("Mojang Approval"), QDialogButtonBox::HelpRole);
    auto* save = buttons->addButton(QObject::tr("Save and Continue"), QDialogButtonBox::AcceptRole);
    save->setDefault(true);
    save->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(learn, &QPushButton::clicked, &dialog, [] {
        DesktopServices::openUrl(QUrl("https://aka.ms/mce-reviewappid"));
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(id, &QLineEdit::textChanged, save, [save](const QString& text) {
        static const QRegularExpression canonical(R"(^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$)");
        const auto value = text.trimmed();
        save->setEnabled(canonical.match(value).hasMatch() && !QUuid(value).isNull());
    });
    id->setFocus();
    if (dialog.exec() != QDialog::Accepted || !save->isEnabled())
        return false;

    APPLICATION->settings()->set("MSAClientIDOverride", id->text().trimmed());
    APPLICATION->updateCapabilities();
    return APPLICATION->capabilities().testFlag(Application::SupportsMSA);
}
}  // namespace

MSALoginDialog::MSALoginDialog(QWidget* parent) : QDialog(parent), ui(new Ui::MSALoginDialog)
{
    ui->setupUi(this);

    // make font monospace
    QFont font;
    font.setPixelSize(ui->code->fontInfo().pixelSize());
    font.setFamily(APPLICATION->settings()->get("ConsoleFont").toString());
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    ui->code->setFont(font);

    connect(ui->copyCode, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(ui->code->text()); });
    connect(ui->loginButton, &QPushButton::clicked, this, [this] {
        if (m_url.isValid()) {
            if (!DesktopServices::openUrl(m_url)) {
                QApplication::clipboard()->setText(m_url.toString());
            }
        }
    });

    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

int MSALoginDialog::exec()
{
    // Setup the login task and start it
    const auto browserAccount = MinecraftAccount::createBlankMSA();
    const auto deviceAccount = MinecraftAccount::createBlankMSA();
    const auto acceptAccount = [this](const MinecraftAccountPtr& account) {
        if (!m_account) {
            m_account = account;
            accept();
        }
    };
    m_authflow_task.reset(new AuthFlow(browserAccount->accountData(), AuthFlow::Action::Login));
    connect(m_authflow_task.get(), &Task::failed, this, &MSALoginDialog::onAuthFlowTaskFailed);
    connect(m_authflow_task.get(), &Task::succeeded, this, [acceptAccount, browserAccount] { acceptAccount(browserAccount); });
    connect(m_authflow_task.get(), &Task::aborted, this, &MSALoginDialog::reject);
    connect(m_authflow_task.get(), &Task::status, this, &MSALoginDialog::onAuthFlowStatus);
    connect(m_authflow_task.get(), &AuthFlow::authorizeWithBrowser, this, &MSALoginDialog::authorizeWithBrowser);
    connect(m_authflow_task.get(), &AuthFlow::authorizeWithBrowserWithExtra, this, &MSALoginDialog::authorizeWithBrowserWithExtra);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_authflow_task.get(), &Task::abort);

    m_devicecode_task.reset(new AuthFlow(deviceAccount->accountData(), AuthFlow::Action::DeviceCode));
    connect(m_devicecode_task.get(), &Task::failed, this, &MSALoginDialog::onDeviceCodeTaskFailed);
    connect(m_devicecode_task.get(), &Task::succeeded, this, [acceptAccount, deviceAccount] { acceptAccount(deviceAccount); });
    connect(m_devicecode_task.get(), &Task::aborted, this, &MSALoginDialog::reject);
    connect(m_devicecode_task.get(), &Task::status, this, &MSALoginDialog::onDeviceFlowStatus);
    connect(m_devicecode_task.get(), &AuthFlow::authorizeWithBrowser, this, &MSALoginDialog::authorizeWithBrowser);
    connect(m_devicecode_task.get(), &AuthFlow::authorizeWithBrowserWithExtra, this, &MSALoginDialog::authorizeWithBrowserWithExtra);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_devicecode_task.get(), &Task::abort);
    QMetaObject::invokeMethod(m_authflow_task.get(), &Task::start, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_devicecode_task.get(), &Task::start, Qt::QueuedConnection);

    return QDialog::exec();
}

MSALoginDialog::~MSALoginDialog()
{
    delete ui;
}

void MSALoginDialog::onAuthFlowTaskFailed(QString reason)
{
    // Set message
    m_authflow_task->disconnect();
    ui->stackedWidget2->setCurrentIndex(0);
    ui->status2->setText(formatError(reason));
    ui->loadingLabel2->setText(m_authflow_task->getStatus());
    disconnect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_authflow_task.get(), &Task::abort);
    if (m_devicecode_task->getState() == Task::State::Failed) {
        disconnect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_devicecode_task.get(), &Task::abort);
        connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &MSALoginDialog::reject,
                Qt::UniqueConnection);
    }
}

void MSALoginDialog::onDeviceCodeTaskFailed(QString reason)
{
    // Set message
    m_devicecode_task->disconnect();
    ui->stackedWidget->setCurrentIndex(0);
    ui->status->setText(formatError(reason));
    ui->loadingLabel->setText(m_devicecode_task->getStatus());
    disconnect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_devicecode_task.get(), &Task::abort);
    if (m_authflow_task->getState() == Task::State::Failed) {
        disconnect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, m_authflow_task.get(), &Task::abort);
        connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &MSALoginDialog::reject,
                Qt::UniqueConnection);
    }
}

void MSALoginDialog::authorizeWithBrowser(const QUrl& url)
{
    ui->stackedWidget2->setCurrentIndex(1);
    ui->stackedWidget2->adjustSize();
    ui->stackedWidget2->updateGeometry();
    this->adjustSize();
    ui->loginButton->setToolTip(QString("<div style='width: 200px;'>%1</div>").arg(url.toString()));
    m_url = url;
}

void paintQR(QPainter& painter, const QSize canvasSize, const QString& data, QColor fg)
{
    Handle<QRcode, QRcode_free> qr{ QRcode_encodeString(data.toUtf8().constData(), 0, QRecLevel::QR_ECLEVEL_M, QRencodeMode::QR_MODE_8,
                                                        1) };
    if (!qr) {
        qWarning() << "Unable to encode" << data << "as QR code";
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fg);

    // Make sure the QR code fits in the canvas with some padding
    const auto qrSize = qr->width;
    const auto canvasWidth = canvasSize.width();
    const auto canvasHeight = canvasSize.height();
    const auto scale = 0.8 * std::min(canvasWidth / qrSize, canvasHeight / qrSize);

    // Find an offset to center it in the canvas
    const auto offsetX = (canvasWidth - qrSize * scale) / 2;
    const auto offsetY = (canvasHeight - qrSize * scale) / 2;

    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            auto shouldFillIn = qr->data[y * qrSize + x] & 1;
            if (shouldFillIn) {
                QRectF r(offsetX + x * scale, offsetY + y * scale, scale, scale);
                painter.drawRects(&r, 1);
            }
        }
    }
}

void MSALoginDialog::authorizeWithBrowserWithExtra(QString url, QString code, [[maybe_unused]] int expiresIn)
{
    ui->stackedWidget->setCurrentIndex(1);
    ui->stackedWidget->adjustSize();
    ui->stackedWidget->updateGeometry();
    this->adjustSize();

    const auto displayUrl = url;
    if (url == "https://www.microsoft.com/link" && !code.isEmpty()) {
        url += QString("?otc=%1").arg(code);
    }
    const auto linkString = QString("<a href=\"%1\">%2</a>").arg(url, displayUrl);
    ui->code->setText(code);

    auto size = QSize(150, 150);
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    paintQR(painter, size, url, Qt::black);

    // Set the generated pixmap to the label
    ui->qr->setPixmap(pixmap);

    ui->qrMessage->setText(tr("Open %1 or scan the QR and enter the above code if needed.").arg(linkString));
}

void MSALoginDialog::onDeviceFlowStatus(QString status)
{
    ui->stackedWidget->setCurrentIndex(0);
    ui->stackedWidget->adjustSize();
    ui->stackedWidget->updateGeometry();
    this->adjustSize();
    ui->status->setText(status);
}

void MSALoginDialog::onAuthFlowStatus(QString status)
{
    ui->stackedWidget2->setCurrentIndex(0);
    ui->stackedWidget2->adjustSize();
    ui->stackedWidget2->updateGeometry();
    this->adjustSize();
    ui->status2->setText(status);
}

// Public interface
MinecraftAccountPtr MSALoginDialog::newAccount(QWidget* parent)
{
    if (!configureMicrosoftApplication(parent))
        return nullptr;

    MSALoginDialog dlg(parent);
    if (dlg.exec() == QDialog::Accepted) {
        return dlg.m_account;
    }
    return nullptr;
}

MinecraftAccountPtr MSALoginDialog::addAccount(QWidget* parent)
{
    auto account = newAccount(parent);
    if (account) {
        auto* accounts = APPLICATION->accounts();
        accounts->addAccount(account);
        if (!accounts->defaultAccount()) {
            accounts->setDefaultAccount(account);
        }
    }
    return account;
}
