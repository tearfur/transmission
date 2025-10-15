// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#pragma once

#include "BaseDialog.h"
#include "ui_LicenseDialog.h"

class LicenseDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(QWidget* parent = nullptr);
    LicenseDialog(LicenseDialog&&) = delete;
    LicenseDialog(LicenseDialog const&) = delete;
    LicenseDialog& operator=(LicenseDialog&&) = delete;
    LicenseDialog& operator=(LicenseDialog const&) = delete;

private:
    Ui::LicenseDialog ui_ = {};
};
