// YM2612 via ymfm (BSD-3, Aaron Giles), two chips for the threaded sound of the X360 port.
// Derived from Reference/StrikeHDCollection/src/hw/ym2612_ymfm.cpp (MIT): same timer counting
// (chip clocks per generated sample), so the game chip's status stays identical to the reference.
#include <cstring>
#include <vector>
#include "ymfm_opn.h"
#include "ym2612_x360.h"

namespace {

class intf : public ymfm::ymfm_interface {
public:
    int32_t timer[2] = { -1, -1 };   // remaining clocks, -1 = stopped
    void ymfm_set_timer(uint32_t tnum, int32_t duration) override { timer[tnum] = duration; }
    void tick(int32_t clocks)
    {
        for (int t = 0; t < 2; t++) {
            if (timer[t] < 0) continue;
            timer[t] -= clocks;
            if (timer[t] <= 0) {
                int32_t over = -timer[t];
                timer[t] = -1;
                m_engine->engine_timer_expired(t);   // re-arms via ymfm_set_timer
                if (timer[t] > 0) timer[t] -= over;
            }
        }
    }
};

// The game chip still runs the engine clock once per sample, with no channel selected: timer B's
// reload depends on the engine's sample counter (update_timer(1, ..., -(m_total_clocks & 15)) in
// ymfm_fm.ipp), so skipping clock() made the Z80 see timer B expire at other times (found by
// Tools/pc_check: zram diverged at frame 149-390). clock(0) prepares and clocks no channel.
class game_chip : public ymfm::ym2612 {
public:
    explicit game_chip(ymfm::ymfm_interface &i) : ymfm::ym2612(i) {}
    void engine_tick() { m_fm.clock(0); }
};

intf g_intf, a_intf;                 // game chip, audio chip
game_chip *g_chip;
ymfm::ym2612 *a_chip;

size_t save(ymfm::ym2612 *chip, intf &i, uint8_t *out)
{
    std::vector<uint8_t> buf;
    ymfm::ymfm_saved_state st(buf, true);
    chip->save_restore(st);
    size_t n = buf.size() + sizeof i.timer;
    if (n > YM_STATE_MAX) return 0;
    memcpy(out, i.timer, sizeof i.timer);
    memcpy(out + sizeof i.timer, buf.data(), buf.size());
    return n;
}

int load(ymfm::ym2612 *chip, intf &i, const uint8_t *in, size_t n)
{
    if (n < sizeof i.timer) return 0;
    memcpy(i.timer, in, sizeof i.timer);
    std::vector<uint8_t> buf(in + sizeof i.timer, in + n);
    ymfm::ymfm_saved_state st(buf, false);
    chip->save_restore(st);
    return 1;
}

}  // namespace

extern "C" void ym_init(void)
{
    if (!g_chip) g_chip = new game_chip(g_intf);
    if (!a_chip) a_chip = new ymfm::ym2612(a_intf);
    ym_reset();
    ym_audio_reset();
}

extern "C" void ym_reset(void)
{
    g_intf.timer[0] = g_intf.timer[1] = -1;
    g_chip->reset();
}

extern "C" void ym_write(int port, uint8_t v) { g_chip->write(port & 3, v); }
extern "C" uint8_t ym_status(void) { return g_chip->read_status(); }
extern "C" void ym_tick(void) { g_chip->engine_tick(); g_intf.tick(144); }

// Single-thread path (same as the reference): the game chip also generates.
extern "C" void ym_sample(int32_t out[2])
{
    ymfm::ym2612::output_data o;
    g_chip->generate(&o, 1);
    g_intf.tick(144);
    out[0] = o.data[0];
    out[1] = o.data[1];
}

extern "C" void ym_audio_reset(void)
{
    a_intf.timer[0] = a_intf.timer[1] = -1;
    a_chip->reset();
}

extern "C" void ym_audio_write(int port, uint8_t v) { a_chip->write(port & 3, v); }

extern "C" void ym_audio_sample(int32_t out[2])
{
    ymfm::ym2612::output_data o;
    a_chip->generate(&o, 1);
    a_intf.tick(144);                  // the audio chip's own timers (CSM key-on follows them)
    out[0] = o.data[0];
    out[1] = o.data[1];
}

extern "C" size_t ym_state_save(uint8_t *out) { return save(g_chip, g_intf, out); }
extern "C" int ym_state_load(const uint8_t *in, size_t n) { return load(g_chip, g_intf, in, n); }
extern "C" int ym_audio_state_load(const uint8_t *in, size_t n) { return load(a_chip, a_intf, in, n); }
