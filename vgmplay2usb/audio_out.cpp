#include "audio_out.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

struct AudioOut::Impl
{
    static const int NBUF = 4;
    static const int FRAMES = 1024;      // ~21 ms at 48 kHz

    HWAVEOUT handle = NULL;
    WAVEHDR hdr[NBUF] = {};
    std::vector<short> data[NBUF];
    std::thread worker;
    std::atomic<bool> run{false};
    std::mutex lock;
    FillFn fill;
    bool active = false;
    int rate = 48000;

    void loop();
    void close();
};

void AudioOut::Impl::loop()
{
    while (run) {
        for (int i = 0; i < NBUF; i++) {
            if (hdr[i].dwFlags & WHDR_INQUEUE) continue;

            bool fed = false;
            {
                std::lock_guard<std::mutex> guard(lock);
                if (active && fill)
                    fed = fill(data[i].data(), FRAMES);
                if (!fed) {
                    active = false;
                    std::memset(data[i].data(), 0, FRAMES * 2 * sizeof(short));
                }
            }

            hdr[i].dwFlags &= ~WHDR_DONE;
            if (waveOutWrite(handle, &hdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                run = false;
                return;
            }
        }
        Sleep(2);
    }
}

bool AudioOut::start(int sampleRate)
{
    if (m_impl && m_impl->handle)
        return true;
    if (!m_impl)
        m_impl = new Impl;
    Impl &im = *m_impl;
    im.rate = sampleRate;

    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = sampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = sampleRate * 4;

    if (waveOutOpen(&im.handle, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        im.handle = NULL;
        return false;
    }

    for (int i = 0; i < Impl::NBUF; i++) {
        im.data[i].assign(Impl::FRAMES * 2, 0);
        ZeroMemory(&im.hdr[i], sizeof(WAVEHDR));
        im.hdr[i].lpData = (LPSTR)im.data[i].data();
        im.hdr[i].dwBufferLength = Impl::FRAMES * 2 * sizeof(short);
        im.hdr[i].dwUser = (DWORD_PTR)i;
        if (waveOutPrepareHeader(im.handle, &im.hdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            im.close();
            return false;
        }
    }

    im.run = true;
    im.worker = std::thread(&Impl::loop, &im);
    return true;
}

void AudioOut::Impl::close()
{
    if (!handle) return;
    run = false;
    waveOutReset(handle);
    if (worker.joinable())
        worker.join();
    for (int i = 0; i < NBUF; i++)
        waveOutUnprepareHeader(handle, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(handle);
    handle = NULL;
    std::lock_guard<std::mutex> guard(lock);
    active = false;
    fill = FillFn();
}

void AudioOut::stop()
{
    if (!m_impl) return;
    m_impl->close();
    delete m_impl;
    m_impl = nullptr;
}

void AudioOut::setFill(FillFn fn)
{
    if (!m_impl) return;
    std::lock_guard<std::mutex> guard(m_impl->lock);
    m_impl->fill = std::move(fn);
    m_impl->active = (bool)m_impl->fill;
}

AudioOut::AudioOut() : m_impl(nullptr)
{
}

AudioOut::~AudioOut()
{
    stop();
}
