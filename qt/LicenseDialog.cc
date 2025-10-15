// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#include <QWidget>

#include "BaseDialog.h"
#include "LicenseDialog.h"

LicenseDialog::LicenseDialog(QWidget* parent)
    : BaseDialog{ parent }
{
    ui_.setupUi(this);
}
