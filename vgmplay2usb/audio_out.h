#pragma once

#include <functional>

// Real-time stereo PCM output through WinMM waveOut.  A small ring of
// buffers is refilled by a worker thread; the fill callback returns false
// when its source has finished, after which the device keeps playing
// silence until another source is installed.
class AudioOut
{
public:
    using FillFn = std::function<bool(short *interleaved, int frames)>;

    AudioOut();
    ~AudioOut();

    bool start(int sampleRate);
    void stop();
    void setFill(FillFn fn);

private:
    struct Impl;
    Impl *m_impl;
};
