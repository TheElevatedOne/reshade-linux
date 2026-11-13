/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Parts of the effect runtime that are not available in the Linux build.
 */

#include "runtime.hpp"

unsigned int reshade::runtime::s_latest_version[3] = {};

void reshade::runtime::check_for_update()
{
	// The update check looks for new Windows releases, which does not apply to this build (updates come through the package manager instead)
}

#if RESHADE_GUI
// VR support (OpenVR/OpenXR) is not available in the Linux build
bool reshade::runtime::init_gui_vr()
{
	return false;
}
void reshade::runtime::deinit_gui_vr()
{
}
void reshade::runtime::draw_gui_vr()
{
}
#endif
