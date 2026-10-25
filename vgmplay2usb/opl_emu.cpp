#include "opl_emu.h"

extern "C" {
#include "nukedopl3.h"
}

#include <cstring>
#include <mutex>

struct Opl3Emu::Impl
{
    opl3_chip chip;
    int rate;
    std::mutex lock;
};

Opl3Emu::Opl3Emu() : m_impl(nullptr)
{
}

Opl3Emu::~Opl3Emu()
{
    delete m_impl;
}

bool Opl3Emu::init(int sampleRate)
{
    delete m_impl;
    m_impl = new Impl;
    m_impl->rate = sampleRate;
    std::memset(&m_impl->chip, 0, sizeof(m_impl->chip));
    OPL3_Reset(&m_impl->chip, (uint32_t)sampleRate);
    return true;
}

void Opl3Emu::reset()
{
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->lock);
    std::memset(&m_impl->chip, 0, sizeof(m_impl->chip));
    OPL3_Reset(&m_impl->chip, (uint32_t)m_impl->rate);
}

void Opl3Emu::write(unsigned addr, unsigned data)
{
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->lock);
    OPL3_WriteRegBuffered(&m_impl->chip, (uint16_t)addr, (uint8_t)data);
}

void Opl3Emu::render(short *interleaved, int frames)
{
    if (!m_impl) {
        std::memset(interleaved, 0, (size_t)frames * 2 * sizeof(short));
        return;
    }
    std::lock_guard<std::mutex> lock(m_impl->lock);
    while (frames-- > 0) {
        int16_t frame[2];
        OPL3_GenerateResampled(&m_impl->chip, frame);
        *interleaved++ = frame[0];
        *interleaved++ = frame[1];
    }
}
