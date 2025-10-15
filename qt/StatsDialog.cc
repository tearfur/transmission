// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#include <QTimer>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "Session.h"
#include "StatsDialog.h"

enum
{
    REFRESH_INTERVAL_MSEC = (15 * 1000)
};

StatsDialog::StatsDialog(Session& session, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
{
    ui_.setupUi(this);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.currentSessionSectionLayout);
    cr->addLayout(ui_.totalSectionLayout);
    cr->update();

    timer_.setSingleShot(false);
    connect(&timer_, &QTimer::timeout, &session_, &Session::refreshSessionStats);
    connect(&session_, &Session::statsUpdated, this, &StatsDialog::updateStats);
    updateStats();
    session_.refreshSessionStats();
}

void StatsDialog::setVisible(bool visible)
{
    timer_.stop();

    if (visible)
    {
        timer_.start(REFRESH_INTERVAL_MSEC);
    }

    BaseDialog::setVisible(visible);
}

void StatsDialog::updateStats()
{
    tr_session_stats const& current(session_.getStats());
    tr_session_stats const& total(session_.getCumulativeStats());

    ui_.currentUploadedValueLabel->setText(Formatter::storage_to_string(current.uploadedBytes));
    ui_.currentDownloadedValueLabel->setText(Formatter::storage_to_string(current.downloadedBytes));
    ui_.currentRatioValueLabel->setText(Formatter::ratio_to_string(current.ratio));
    ui_.currentDurationValueLabel->setText(Formatter::time_to_string(current.secondsActive));

    ui_.totalUploadedValueLabel->setText(Formatter::storage_to_string(total.uploadedBytes));
    ui_.totalDownloadedValueLabel->setText(Formatter::storage_to_string(total.downloadedBytes));
    ui_.totalRatioValueLabel->setText(Formatter::ratio_to_string(total.ratio));
    ui_.totalDurationValueLabel->setText(Formatter::time_to_string(total.secondsActive));

    ui_.startCountLabel->setText(tr("Started %Ln time(s)", nullptr, total.sessionCount));
}
