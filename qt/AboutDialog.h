// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#pragma once

#include <QPointer>

#include "BaseDialog.h"
#include "ui_AboutDialog.h"

class LicenseDialog;
class Session;

class AboutDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(Session& session, QWidget* parent = nullptr);
    AboutDialog(AboutDialog&&) = delete;
    AboutDialog(AboutDialog const&) = delete;
    AboutDialog& operator=(AboutDialog&&) = delete;
    AboutDialog& operator=(AboutDialog const&) = delete;

private slots:
    void showCredits();
    void showLicense();

private:
    Ui::AboutDialog ui_{};

    QPointer<LicenseDialog> license_dialog_;
};
