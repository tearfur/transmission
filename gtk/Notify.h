// This file Copyright © ReTransmission contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <glibmm/refptr.h>

class Session;

void gtr_notify_init();

void gtr_notify_torrent_added(Glib::RefPtr<Session> const& core, tr_torrent_id_t tor_id);

void gtr_notify_torrent_completed(Glib::RefPtr<Session> const& core, tr_torrent_id_t tor_id);
