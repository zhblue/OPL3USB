#pragma once

// Thin wrapper around the Nuked OPL3 core (YMf262).  Register writes come
// from the VGM player thread, sample generation runs on the audio thread, so
// both entry points are serialised with a mutex.
class Opl3Emu
{
public:
    Opl3Emu();
    ~Opl3Emu();

    bool init(int sampleRate);
    void reset();
    void write(unsigned addr, unsigned data);
    void render(short *interleaved, int frames);

private:
    struct Impl;
    Impl *m_impl;
};
