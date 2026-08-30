-- license:BSD-3-Clause
-- SPDX-License-Identifier: BSD-3-Clause
-- copyright-holders:Vintage Emulator Studio contributors

STANDALONE = true

CPUS["M6800"] = true
CPUS["I86"] = true
CPUS["H8"] = true
CPUS["H8500"] = true
CPUS["DSPV"] = true
CPUS["ES5510"] = true
CPUS["M6502"] = true
CPUS["M6809"] = true
CPUS["M680X0"] = true
CPUS["MCS48"] = true
CPUS["MCS51"] = true
CPUS["M37710"] = true
CPUS["M68HC16"] = true
CPUS["MC68HC11"] = true
CPUS["NEC"] = true
CPUS["SH"] = true
CPUS["SWP30"] = true
CPUS["UPD7810"] = true
CPUS["Z80"] = true

MACHINES["CLOCK"] = true
MACHINES["NVRAM"] = true
MACHINES["MIDI"] = true
MACHINES["ACIA6850"] = true
MACHINES["ADC0808"] = true
MACHINES["AM9517A"] = true
MACHINES["CXD1185"] = true
MACHINES["TTL7474"] = true
MACHINES["TTL74259"] = true
MACHINES["EEPROM28"] = true
MACHINES["EEPROMDEV"] = true
MACHINES["FDC_PLL"] = true
MACHINES["GT913"] = true
MACHINES["HD63450"] = true
MACHINES["I8243"] = true
MACHINES["I8251"] = true
MACHINES["I8255"] = true
MACHINES["MB89371"] = true
MACHINES["INPUT_MERGER"] = true
MACHINES["68681"] = true
MACHINES["MSM6200"] = true
MACHINES["MB87030"] = true
MACHINES["NSCSI"] = true
MACHINES["NCR5380"] = true
MACHINES["NCR53C90"] = true
MACHINES["OUTPUT_LATCH"] = true
MACHINES["PIT8253"] = true
MACHINES["PIC8259"] = true
MACHINES["SCI4"] = true
MACHINES["UPD765"] = true
MACHINES["TE7774"] = true
MACHINES["WD_FDC"] = true
MACHINES["WD33C9X"] = true
MACHINES["Z80DAISY"] = true

SOUNDS["SPEAKER"] = true
SOUNDS["YM2414"] = true
SOUNDS["YMOPM"] = true
SOUNDS["DAC"] = true
SOUNDS["ADC"] = true
SOUNDS["BEEP"] = true
SOUNDS["CEM3310"] = true
SOUNDS["CEM3320"] = true
SOUNDS["CEM3340"] = true
SOUNDS["CEM3360"] = true
SOUNDS["CEM3394"] = true
SOUNDS["CDDA"] = true
SOUNDS["DAC76"] = true
SOUNDS["ES5503"] = true
SOUNDS["ES5505"] = true
SOUNDS["ESQPUMP"] = true
SOUNDS["GEW7"] = true
SOUNDS["MEG"] = true
SOUNDS["L7A1045"] = true
SOUNDS["L4003"] = true
SOUNDS["MULTIPCM"] = true
SOUNDS["MM5837"] = true
SOUNDS["SWP00"] = true
SOUNDS["SWX00"] = true
SOUNDS["UPD931"] = true
SOUNDS["UPD933"] = true
SOUNDS["UPD934G"] = true
SOUNDS["VA_EG"] = true
SOUNDS["VA_OPS"] = true
SOUNDS["VA_VCA"] = true
SOUNDS["VA_VCF"] = true
SOUNDS["VA_VCO"] = true
SOUNDS["YM2151"] = true
SOUNDS["YM2154"] = true
SOUNDS["YM3812"] = true
SOUNDS["YM3806"] = true

VIDEOS["HD44780"] = true
VIDEOS["HD61830"] = true
VIDEOS["HD61602"] = true
VIDEOS["DL1416"] = true
VIDEOS["MN1252"] = true
VIDEOS["PWM_DISPLAY"] = true
VIDEOS["T6963C"] = true

BUSES["GENERIC"] = true
BUSES["NSCSI"] = true
BUSES["PLG1X0"] = true

FORMATS["ESQ16_DSK"] = true
FORMATS["TRS_CAS"] = true

function standalone()
	defines {
		"VES_EMBEDDED_BUILD",
		"VES_VIRTUAL_MIDI_RETROFIT",
		"VES_LAYOUT_PLUGIN_SUPPORT",
	}

	includedirs {
		MAME_DIR .. "src",
		MAME_DIR .. "src/frontend/mame",
		MAME_DIR .. "3rdparty/sol2",
		GEN_DIR  .. "emu",
		GEN_DIR  .. "emu/layout",
		ext_includedir("asio"),
		ext_includedir("expat"),
		ext_includedir("lua"),
		ext_includedir("zlib"),
		ext_includedir("flac"),
		ext_includedir("rapidjson"),
	}

	links {
		ext_lib("lua"),
		"lualibs",
		"linenoise",
		ext_lib("sqlite3"),
	}

	files {
		MAME_DIR .. "src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp",
		MAME_DIR .. "src/ves/embeddedinstruments/EmbeddedEmulatorEngine.h",
		MAME_DIR .. "src/ves/embeddedinstruments/main.cpp",
		MAME_DIR .. "src/frontend/mame/luaengine.cpp",
		MAME_DIR .. "src/frontend/mame/luaengine.h",
		MAME_DIR .. "src/frontend/mame/luaengine_render.cpp",
		MAME_DIR .. "src/frontend/mame/pluginopts.cpp",
		MAME_DIR .. "src/frontend/mame/pluginopts.h",
		MAME_DIR .. "src/mame/casio/ct8000.cpp",
		MAME_DIR .. "src/mame/akai/s3000.cpp",
		MAME_DIR .. "src/mame/akai/mpc60.cpp",
		MAME_DIR .. "src/mame/akai/mpc3000.cpp",
		MAME_DIR .. "src/mame/casio/ct8000_midi.cpp",
		MAME_DIR .. "src/mame/casio/ct8000_midi.h",
		MAME_DIR .. "src/mame/casio/ctk551.cpp",
		MAME_DIR .. "src/mame/casio/cz1.cpp",
		MAME_DIR .. "src/mame/casio/cz101.cpp",
		MAME_DIR .. "src/mame/casio/cz230s.cpp",
		MAME_DIR .. "src/mame/casio/ra3.cpp",
		MAME_DIR .. "src/mame/casio/ra3.h",
		MAME_DIR .. "src/mame/casio/rz1.cpp",
		MAME_DIR .. "src/mame/ensoniq/esq1.cpp",
		MAME_DIR .. "src/mame/ensoniq/esq5505.cpp",
		MAME_DIR .. "src/mame/ensoniq/esqkt.cpp",
		MAME_DIR .. "src/mame/ensoniq/esqlcd.cpp",
		MAME_DIR .. "src/mame/ensoniq/esqlcd.h",
		MAME_DIR .. "src/mame/ensoniq/esqpanel.cpp",
		MAME_DIR .. "src/mame/ensoniq/esqpanel.h",
		MAME_DIR .. "src/mame/ensoniq/esqvfd.cpp",
		MAME_DIR .. "src/mame/ensoniq/esqvfd.h",
		MAME_DIR .. "src/mame/ensoniq/vfxcart.cpp",
		MAME_DIR .. "src/mame/ensoniq/vfxcart.h",
		MAME_DIR .. "src/mame/paia/fatman.cpp",
		MAME_DIR .. "src/mame/linn/linndrum.cpp",
		MAME_DIR .. "src/mame/oberheim/dmx.cpp",
		MAME_DIR .. "src/mame/roland/mb63h114.cpp",
		MAME_DIR .. "src/mame/roland/mb63h114.h",
		MAME_DIR .. "src/mame/roland/roland_tr707.cpp",
		MAME_DIR .. "src/mame/sequential/prophet5.cpp",
		MAME_DIR .. "src/mame/sequential/sixtrak.cpp",
		MAME_DIR .. "src/mame/yamaha/fb01.cpp",
		MAME_DIR .. "src/mame/yamaha/tg100.cpp",
		MAME_DIR .. "src/mame/yamaha/mulcd.cpp",
		MAME_DIR .. "src/mame/yamaha/mulcd.h",
		MAME_DIR .. "src/mame/yamaha/ymdx100.cpp",
		MAME_DIR .. "src/mame/yamaha/ymmu2000.cpp",
		MAME_DIR .. "src/mame/yamaha/ymmu50.cpp",
		MAME_DIR .. "src/mame/yamaha/ympsr150.cpp",
		MAME_DIR .. "src/mame/yamaha/ympsr11.cpp",
		MAME_DIR .. "src/mame/yamaha/ympsr60.cpp",
		MAME_DIR .. "src/mame/yamaha/ymtx81z.cpp",
		MAME_DIR .. "src/devices/bus/midi/midi.cpp",
		MAME_DIR .. "src/devices/bus/midi/midi.h",
		MAME_DIR .. "src/devices/bus/midi/midiinport.cpp",
		MAME_DIR .. "src/devices/bus/midi/midiinport.h",
		MAME_DIR .. "src/devices/bus/midi/midioutport.cpp",
		MAME_DIR .. "src/devices/bus/midi/midioutport.h",
		MAME_DIR .. "src/devices/machine/gt913_io.cpp",
		MAME_DIR .. "src/devices/machine/gt913_kbd.cpp",
		MAME_DIR .. "src/devices/machine/gt913_snd.cpp",
		MAME_DIR .. "src/devices/video/pwm.cpp",
		MAME_DIR .. "src/devices/imagedev/midiin.cpp",
		MAME_DIR .. "src/devices/imagedev/midiin.h",
		MAME_DIR .. "src/devices/imagedev/midiout.cpp",
		MAME_DIR .. "src/devices/imagedev/midiout.h",
	}
end
