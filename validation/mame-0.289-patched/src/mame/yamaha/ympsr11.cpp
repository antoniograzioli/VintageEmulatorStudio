// license:BSD-3-Clause
// copyright-holders:Carl

#include "emu.h"
#include "cpu/m6800/m6801.h"
#include "sound/ymopl.h"
#include "speaker.h"

#if defined(VES_VIRTUAL_MIDI_RETROFIT)
#include "bus/midi/midi.h"
#include "machine/clock.h"
#include "machine/i8251.h"
#endif

#include "psr11.lh"

namespace {

#if defined(VES_VIRTUAL_MIDI_RETROFIT)
struct virtual_midi_key { u8 port, mask; };
constexpr std::array<virtual_midi_key, 49> virtual_midi_key_map = {{
	{7,0x40},{7,0x20},{7,0x10},{7,0x08},{7,0x04},{7,0x02},{7,0x01},
	{6,0x20},{6,0x10},{6,0x08},{6,0x04},{6,0x02},{6,0x01},
	{5,0x20},{5,0x10},{5,0x08},{5,0x04},{5,0x02},{5,0x01},
	{4,0x20},{4,0x10},{4,0x08},{4,0x04},{4,0x02},{4,0x01},
	{3,0x20},{3,0x10},{3,0x08},{3,0x04},{3,0x02},{3,0x01},
	{2,0x20},{2,0x10},{2,0x08},{2,0x04},{2,0x02},{2,0x01},
	{1,0x20},{1,0x10},{1,0x08},{1,0x04},{1,0x02},{1,0x01},
	{0,0x20},{0,0x10},{0,0x08},{0,0x04},{0,0x02},{0,0x01}
}};
#endif

class yamaha_psr11_state : public driver_device
{
public:
	yamaha_psr11_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_ym3812(*this, "opl2")
#if defined(VES_VIRTUAL_MIDI_RETROFIT)
		, m_virtual_midi_uart(*this, "ves_virtual_midi_uart")
#endif
		, m_keys(*this, "P%u", 0U)
		, m_tempo_led(*this, "led")
	{
	}

	void psr11(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void map(address_map &map) ATTR_COLD;

	u8 p2_r();

#if defined(VES_VIRTUAL_MIDI_RETROFIT)
	void virtual_midi_rxrdy_w(int state);
	void virtual_midi_byte_w(u8 data);
	void virtual_midi_note_w(u8 note, bool pressed);
	void clear_virtual_midi_state();
	void virtual_midi_machine_stop();
#endif

	required_device<hd6301y0_cpu_device> m_maincpu;
	required_device<ym3812_device> m_ym3812;
#if defined(VES_VIRTUAL_MIDI_RETROFIT)
	required_device<i8251_device> m_virtual_midi_uart;
	std::array<u8, 8> m_virtual_midi_ports{};
	std::array<u8, 49> m_virtual_midi_note_counts{};
	u8 m_virtual_midi_running_status = 0;
	u8 m_virtual_midi_data[2]{};
	u8 m_virtual_midi_data_count = 0;
#endif
	required_ioport_array<17> m_keys;
	output_finder<> m_tempo_led;

	u8 m_p5 = 0, m_p6 = 0;
};

void yamaha_psr11_state::machine_start()
{
	save_item(NAME(m_p5));
	save_item(NAME(m_p6));
#if defined(VES_VIRTUAL_MIDI_RETROFIT)
	save_item(NAME(m_virtual_midi_ports));
	save_item(NAME(m_virtual_midi_note_counts));
	save_item(NAME(m_virtual_midi_running_status));
	save_item(NAME(m_virtual_midi_data));
	save_item(NAME(m_virtual_midi_data_count));
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&yamaha_psr11_state::virtual_midi_machine_stop, this));
#endif
}

void yamaha_psr11_state::machine_reset()
{
#if defined(VES_VIRTUAL_MIDI_RETROFIT)
	clear_virtual_midi_state();
	m_virtual_midi_uart->control_w(0x4e);
	m_virtual_midi_uart->control_w(0x04);
#endif
}

void yamaha_psr11_state::map(address_map &map)
{
	map(0x1ffe, 0x1fff).rw(m_ym3812, FUNC(ym3812_device::read), FUNC(ym3812_device::write)); // Only bits 12 and 15 are used to decode the address
}

u8 yamaha_psr11_state::p2_r()
{
	int i, p6 = m_p6 & 0x3f;

	if(p6 == 0x30)
	{
		if(!m_p5)
			return m_keys[16]->read();
		else
			return 0;
	}
	if(((p6 & 0x30) == 0x20) && (p6 & 0xf))
	{
		if(!m_p5)
		{
			for(i = 0; i < 4; i++)
			{
				if(p6 & (1 << i))
					break;
			}
			return m_keys[i + 12]->read();
		}
		else
			return 0;
	}
	for(i = 0; i < 8; i++)
	{
		if(m_p5 & (1 << i))
			break;
	}
	if((p6 == 0x20) && (i < 4))
		return m_keys[i + 8]->read();
	else if(!p6 && (i < 8))
	{
		u8 physical = m_keys[i]->read();
#if defined(VES_VIRTUAL_MIDI_RETROFIT)
		physical |= m_virtual_midi_ports[i];
#endif
		return physical;
	}

	return 0;
}

#if defined(VES_VIRTUAL_MIDI_RETROFIT)
void yamaha_psr11_state::virtual_midi_rxrdy_w(int state) { if (state) virtual_midi_byte_w(m_virtual_midi_uart->data_r()); }
void yamaha_psr11_state::virtual_midi_note_w(u8 note, bool pressed)
{
	if (note < 36 || note > 84) return;
	const unsigned index = note - 36; const auto key = virtual_midi_key_map[index]; u8 &count = m_virtual_midi_note_counts[index];
	if (pressed) { if (count != 0xff) ++count; m_virtual_midi_ports[key.port] |= key.mask; }
	else if (count && --count == 0) m_virtual_midi_ports[key.port] &= ~key.mask;
}
void yamaha_psr11_state::clear_virtual_midi_state()
{
	m_virtual_midi_ports.fill(0); m_virtual_midi_note_counts.fill(0); m_virtual_midi_running_status = 0; m_virtual_midi_data[0] = m_virtual_midi_data[1] = m_virtual_midi_data_count = 0;
}
void yamaha_psr11_state::virtual_midi_byte_w(u8 data)
{
	if (data >= 0xf8) return;
	if (data & 0x80) { m_virtual_midi_running_status = data < 0xf0 ? data : 0; m_virtual_midi_data_count = 0; return; }
	if (!m_virtual_midi_running_status) return;
	const u8 type = m_virtual_midi_running_status & 0xf0, size = (type == 0xc0 || type == 0xd0) ? 1 : 2;
	m_virtual_midi_data[m_virtual_midi_data_count++] = data; if (m_virtual_midi_data_count != size) return;
	if (type == 0x80) virtual_midi_note_w(m_virtual_midi_data[0], false);
	else if (type == 0x90) virtual_midi_note_w(m_virtual_midi_data[0], m_virtual_midi_data[1] != 0);
	else if (type == 0xb0 && (m_virtual_midi_data[0] == 120 || m_virtual_midi_data[0] == 123)) { m_virtual_midi_ports.fill(0); m_virtual_midi_note_counts.fill(0); }
	m_virtual_midi_data_count = 0;
}
void yamaha_psr11_state::virtual_midi_machine_stop() { clear_virtual_midi_state(); }
#endif

static INPUT_PORTS_START(psr11)
	PORT_START("P0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C 6")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B 5")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A#5")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A 5")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G#5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G 5")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F#5")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F 5")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E 5")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D#5")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C#5")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C 5")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B 4")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A#4")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G#4")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G 4")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F#4")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F 4")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E 4")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D#4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D 4")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C#4")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C 4")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B 3")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A#3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G#3")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G 3")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F#3")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F 3")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D#3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D 3")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C#3")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C 3")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A#2")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A 2")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G#2")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G 2")
	PORT_BIT(0xC0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P7")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F#2")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E 2")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D#2")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D 2")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C#2")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C 2")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("P8")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BOSSANOVA")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SAMBA")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("HEAVY METAL")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DISCO")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("WALTZ")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MARCH/POLKA")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("16 BEAT")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("POPS")

	PORT_START("P9")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SYNCHRO START")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("START")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("STOP")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SUSTAIN ON")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SUSTAIN OFF")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("VIBRATO ON")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("VIBRATO OFF")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P98")

	PORT_START("P10")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PIANO")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BRASS 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("JAZZ ORGAN")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("COSMIC")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("HARPSICHORD")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("FLUTE")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PIPE ORGAN")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("POPSYNTH")

	PORT_START("P11")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("JAZZ GUITAR")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CLARINET")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("STRINGS")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("FUNKSYNTH")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("VIBES")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MUSIC BOX")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BRASS 1")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PERCUS")

	PORT_START("P12")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("AUTO BASS VOLUME MIN")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("AUTO BASS VOLUME 1")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("AUTO BASS VOLUME 2")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("AUTO BASS VOLUME 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("AUTO BASS VOLUME MAX")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P126")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PITCH UP")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PITCH DOWN")

	PORT_START("P13")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BASS BASS")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BASS E BASS")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BASS TUBA")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BASS SYNTH")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CHORD PIANO")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CHORD GUITAR")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CHORD BRASS")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CHORD SYNTH")

	PORT_START("P14")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MODE OFF")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SINGLE FINGER")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("FINGERED")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MANUAL BASS")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P145")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("FILL IN")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P147")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DEMONSTRATION")

	PORT_START("P15")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHYTHM VOLUME MIN")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHYTHM VOLUME 1")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHYTHM VOLUME 2")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHYTHM VOLUME 3")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHYTHM VOLUME MAX")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P156")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TEMPO UP")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TEMPO DOWN")

	PORT_START("P16")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("COUNTRY")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("ROCK N ROLL")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SWING")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("BIG BAND")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RHUMBA")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SALSA")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SLOW ROCK")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("REGGAE")
INPUT_PORTS_END

void yamaha_psr11_state::psr11(machine_config &config)
{
	HD6301Y0(config, m_maincpu, 3.579545_MHz_XTAL); // Yamaha XA909A0
	m_maincpu->set_addrmap(AS_PROGRAM, &yamaha_psr11_state::map);
	m_maincpu->in_p2_cb().set(FUNC(yamaha_psr11_state::p2_r));
	m_maincpu->out_p5_cb().set([this](u8 d){ m_p5 = d; });
	m_maincpu->out_p6_cb().set([this](u8 d){ m_p6 = d; m_tempo_led = BIT(d, 6); }); // bit 7 is unconnected on the board

	SPEAKER(config, "mono").front_center();
	YM3812(config, m_ym3812, 3.579545_MHz_XTAL).add_route(ALL_OUTPUTS, "mono", 3.00);

#if defined(VES_VIRTUAL_MIDI_RETROFIT)
	// VES Virtual MIDI Retrofit: contributes only to the existing key scan.
	I8251(config, m_virtual_midi_uart, 0);
	m_virtual_midi_uart->rxrdy_handler().set(FUNC(yamaha_psr11_state::virtual_midi_rxrdy_w));
	CLOCK(config, "ves_virtual_midi_clock", 500_kHz_XTAL).signal_handler().set(m_virtual_midi_uart, FUNC(i8251_device::write_rxc));
	MIDI_PORT(config, "ves_virtual_midiin", midiin_slot, "midiin").rxd_handler().set(m_virtual_midi_uart, FUNC(i8251_device::write_rxd));
#endif

	config.set_default_layout(layout_psr11);
}

ROM_START(psr11)
	ROM_REGION(0x4000, "maincpu", 0)
	ROM_LOAD("yamaha6301y0a72.ic1", 0x0000, 0x4000, CRC(83937c8a) SHA1(5e9263d010dddd2d90c4791f2260b5fc9ec50fd4))
ROM_END

} // anonymous namespace

SYST(1986, psr11, 0, 0, psr11, psr11, yamaha_psr11_state, empty_init, "Yamaha", "Portatone PSR-11", MACHINE_SUPPORTS_SAVE)
