// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#pragma once

#include <QToolButton>

class IconToolButton : public QToolButton
{
    Q_OBJECT

public:
    explicit IconToolButton(QWidget* parent = nullptr);
    IconToolButton(IconToolButton&&) = delete;
    IconToolButton(IconToolButton const&) = delete;
    IconToolButton& operator=(IconToolButton&&) = delete;
    IconToolButton& operator=(IconToolButton const&) = delete;

    // QWidget
    [[nodiscard]] QSize sizeHint() const override;

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;
};
