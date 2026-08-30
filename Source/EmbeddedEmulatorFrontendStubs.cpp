// license:BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Vintage Emulator Studio contributors

#include "emu.h"
#include "main.h"

namespace ves {
void dispatch_embedded_layout_script(layout_file &file, const char *script);
}

#include <map>
#include <string>
#include <utility>
#include <vector>

const char *emulator_info::get_appname() { return "VintageEmulatorStudio"; }
const char *emulator_info::get_appname_lower() { return "ves"; }
const char *emulator_info::get_configname() { return "ves"; }
const char *emulator_info::get_copyright() { return ""; }
const char *emulator_info::get_copyright_info() { return ""; }
const char *emulator_info::get_bare_build_version() { return "0.288"; }
const char *emulator_info::get_build_version() { return "0.288 VintageEmulatorStudio"; }
void emulator_info::display_ui_chooser(running_machine &) {}
int emulator_info::start_frontend(emu_options &, osd_interface &, std::vector<std::string> &) { return 0; }
int emulator_info::start_frontend(emu_options &, osd_interface &, int, char *[]) { return 0; }
void emulator_info::sound_hook(const std::map<std::string, std::vector<std::pair<const float *, int>>> &) {}
bool emulator_info::draw_user_interface(running_machine &) { return false; }
void emulator_info::periodic_check() {}
bool emulator_info::frame_hook() { return false; }
void emulator_info::layout_script_cb(layout_file &file, const char *script)
{
    ves::dispatch_embedded_layout_script(file, script);
}
bool emulator_info::standalone() { return true; }
