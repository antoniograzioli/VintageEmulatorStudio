# Linux-only VES/MAME integration. MAME artifacts are produced by PHASE 1.
VES_MAME_ROOT := $(abspath ../../validation/mame-0.289-patched)
VES_MAME_OBJ := $(VES_MAME_ROOT)/build/linux_gcc/obj/x64/Release
VES_MAME_LIB := $(VES_MAME_ROOT)/build/linux_gcc/bin/x64/Release
VES_MAME_SUBTARGET_LIB := $(VES_MAME_LIB)/mame_vesembedded

JUCE_CPPFLAGS += \
  -DVES_EMBEDDED_BUILD=1 \
  -DVES_VIRTUAL_MIDI_RETROFIT=1 \
  -DVES_LAYOUT_PLUGIN_SUPPORT=1 \
  -DJUCE_INCLUDE_FLAC_CODE=0 \
  -D__STDC_CONSTANT_MACROS \
  -D__STDC_LIMIT_MACROS \
  -I$(VES_MAME_ROOT)/src \
  -I$(VES_MAME_ROOT)/src/frontend/mame \
  -I$(VES_MAME_ROOT)/src/emu \
  -I$(VES_MAME_ROOT)/src/osd \
  -I$(VES_MAME_ROOT)/src/devices \
  -I$(VES_MAME_ROOT)/src/lib \
  -I$(VES_MAME_ROOT)/src/lib/util \
  -I$(VES_MAME_ROOT)/3rdparty \
  -I$(VES_MAME_ROOT)/3rdparty/asmjit \
  -I$(VES_MAME_ROOT)/3rdparty/flac/include \
  -I$(VES_MAME_ROOT)/3rdparty/sol2 \
  -I$(VES_MAME_ROOT)/3rdparty/lua/src \
  -I$(VES_MAME_ROOT)/3rdparty/rapidjson/include \
  -I$(VES_MAME_ROOT)/build/generated \
  -I$(VES_MAME_ROOT)/build/generated/emu \
  -I$(VES_MAME_ROOT)/build/generated/emu/layout \
  -I$(VES_MAME_ROOT)/build/generated/mame \
  -I$(VES_MAME_ROOT)/build/generated/mame/layout

VES_MAME_DRIVER_OBJECTS := \
  $(VES_MAME_OBJ)/src/mame/akai/mpc3000.o \
  $(VES_MAME_OBJ)/src/mame/akai/mpc60.o \
  $(VES_MAME_OBJ)/src/mame/akai/s3000.o \
  $(VES_MAME_OBJ)/src/mame/casio/ct8000.o \
  $(VES_MAME_OBJ)/src/mame/casio/ct8000_midi.o \
  $(VES_MAME_OBJ)/src/mame/casio/ctk551.o \
  $(VES_MAME_OBJ)/src/mame/casio/cz1.o \
  $(VES_MAME_OBJ)/src/mame/casio/cz101.o \
  $(VES_MAME_OBJ)/src/mame/casio/cz230s.o \
  $(VES_MAME_OBJ)/src/mame/casio/ra3.o \
  $(VES_MAME_OBJ)/src/mame/casio/rz1.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esq1.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esq5505.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esqkt.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esqlcd.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esqpanel.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/esqvfd.o \
  $(VES_MAME_OBJ)/src/mame/ensoniq/vfxcart.o \
  $(VES_MAME_OBJ)/src/mame/linn/linndrum.o \
  $(VES_MAME_OBJ)/src/mame/oberheim/dmx.o \
  $(VES_MAME_OBJ)/src/mame/paia/fatman.o \
  $(VES_MAME_OBJ)/src/mame/roland/mb63h114.o \
  $(VES_MAME_OBJ)/src/mame/roland/roland_tr707.o \
  $(VES_MAME_OBJ)/src/mame/sequential/prophet5.o \
  $(VES_MAME_OBJ)/src/mame/sequential/sixtrak.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/fb01.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/mulcd.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/tg100.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ymdx100.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ymmu2000.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ymmu50.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ympsr11.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ympsr150.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ympsr60.o \
  $(VES_MAME_OBJ)/src/mame/yamaha/ymtx81z.o

VES_MAME_DEVICE_OBJECTS := \
  $(VES_MAME_OBJ)/src/devices/bus/midi/midi.o \
  $(VES_MAME_OBJ)/src/devices/bus/midi/midiinport.o \
  $(VES_MAME_OBJ)/src/devices/bus/midi/midioutport.o \
  $(VES_MAME_OBJ)/src/devices/imagedev/midiin.o \
  $(VES_MAME_OBJ)/src/devices/imagedev/midiout.o \
  $(VES_MAME_OBJ)/src/devices/machine/gt913_io.o \
  $(VES_MAME_OBJ)/src/devices/machine/gt913_kbd.o \
  $(VES_MAME_OBJ)/src/devices/machine/gt913_snd.o \
  $(VES_MAME_OBJ)/src/devices/video/pwm.o

VES_MAME_ARCHIVES := \
  $(VES_MAME_SUBTARGET_LIB)/liboptional.a \
  $(VES_MAME_LIB)/libemu.a \
  $(VES_MAME_LIB)/libosd_sdl.a \
  $(VES_MAME_LIB)/libqtdbg_sdl.a \
  $(VES_MAME_SUBTARGET_LIB)/libformats.a \
  $(VES_MAME_SUBTARGET_LIB)/libdasm.a \
  $(VES_MAME_LIB)/libutils.a \
  $(VES_MAME_LIB)/libexpat.a \
  $(VES_MAME_LIB)/libsoftfloat3.a \
  $(VES_MAME_LIB)/libwdlfft.a \
  $(VES_MAME_LIB)/libymfm.a \
  $(VES_MAME_LIB)/libjpeg.a \
  $(VES_MAME_LIB)/lib7z.a \
  $(VES_MAME_LIB)/libasmjit.a \
  $(VES_MAME_LIB)/libzlib.a \
  $(VES_MAME_LIB)/libzstd.a \
  $(VES_MAME_LIB)/libflac.a \
  $(VES_MAME_LIB)/libutf8proc.a \
  $(VES_MAME_LIB)/libportmidi.a \
  $(VES_MAME_LIB)/libbgfx.a \
  $(VES_MAME_LIB)/libbimg.a \
  $(VES_MAME_LIB)/libbx.a \
  $(VES_MAME_LIB)/libocore_sdl.a \
  $(VES_MAME_LIB)/liblua.a \
  $(VES_MAME_LIB)/liblualibs.a \
  $(VES_MAME_LIB)/liblinenoise.a \
  $(VES_MAME_LIB)/libsqlite3.a

VES_MAME_SYSTEM_LIBS := \
  -ldl -lrt -lSDL2 -lm -lpthread -lutil -lGL -lasound \
  -lX11 -lXinerama -lXext -lXi -lSDL2_ttf -lfontconfig -lfreetype

VES_MAME_LINK_PAYLOAD := \
  -Wl,--start-group \
  $(JUCE_OUTDIR)/$(JUCE_TARGET_SHARED_CODE) \
  $(VES_MAME_DRIVER_OBJECTS) \
  $(VES_MAME_DEVICE_OBJECTS) \
  $(VES_MAME_ARCHIVES) \
  -Wl,--end-group \
  $(VES_MAME_SYSTEM_LIBS)

# Apply the same known-good MAME payload to Standalone, VST3, and JUCE's VST3
# manifest helper without adding it to unrelated Projucer helper utilities.
JUCE_LDFLAGS_STANDALONE_PLUGIN += $(VES_MAME_LINK_PAYLOAD)
JUCE_LDFLAGS_VST3 += $(VES_MAME_LINK_PAYLOAD)
JUCE_LDFLAGS_VST3_MANIFEST_HELPER += $(VES_MAME_LINK_PAYLOAD)

.PHONY: ves_standalone_resources
Standalone: ves_standalone_resources

ves_standalone_resources:
	@echo Staging "Vintage Emulator Studio - Standalone Resources"
	-$(V_AT)mkdir -p $(JUCE_OUTDIR)/Resources
	$(V_AT)rm -rf $(JUCE_OUTDIR)/Resources/plugins $(JUCE_OUTDIR)/Resources/artwork
	$(V_AT)cp -R ../../Resources/plugins $(JUCE_OUTDIR)/Resources/
	$(V_AT)cp -R ../../artwork $(JUCE_OUTDIR)/Resources/
	$(V_AT)find $(JUCE_OUTDIR)/Resources -type f \( -name '.DS_Store' -o -name '._*' -o -name '*.psd' \) -delete

VES_VST3_RESOURCE_DIR := $(JUCE_OUTDIR)/$(JUCE_VST3DIR)/Contents/Resources

.PHONY: ves_vst3_resources
$(JUCE_OUTDIR)/$(JUCE_TARGET_VST3): ves_vst3_resources

ves_vst3_resources:
	@echo Staging "Vintage Emulator Studio - VST3 Resources"
	-$(V_AT)mkdir -p $(VES_VST3_RESOURCE_DIR)
	$(V_AT)rm -rf $(VES_VST3_RESOURCE_DIR)/plugins $(VES_VST3_RESOURCE_DIR)/artwork
	$(V_AT)cp -R ../../Resources/plugins $(VES_VST3_RESOURCE_DIR)/
	$(V_AT)cp -R ../../artwork $(VES_VST3_RESOURCE_DIR)/
	$(V_AT)find $(VES_VST3_RESOURCE_DIR) -type f \( -name '.DS_Store' -o -name '._*' -o -name '*.psd' \) -delete
