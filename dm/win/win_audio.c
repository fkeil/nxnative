#include "win_posix.h"
#include <AudioToolbox/AudioToolbox.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_AUDIO_FRAMES 240 // 5 ms @ 48000 Hz

static const GUID my_CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID my_IID_IMMDeviceEnumerator  = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID my_IID_IAudioClient         = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static const GUID my_IID_IAudioRenderClient   = { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };

typedef struct win_audio_unit_s {
    AURenderCallback cb;
    void *cb_ref;
    uint32_t guest_rate;
    uint32_t guest_channels;

    IMMDeviceEnumerator *pEnum;
    IMMDevice *pDev;
    IAudioClient *pClient;
    IAudioRenderClient *pRender;
    WAVEFORMATEX *pMixFormat;
    UINT32 buffer_frames;

    HANDLE h_event;
    HANDLE h_thread;
    _Atomic int running;

    int16_t *pcm_scratch;
    uint32_t pcm_scratch_bytes;
} win_audio_unit_t;

static DWORD WINAPI win_wasapi_thread(LPVOID arg) {
    win_audio_unit_t *au = (win_audio_unit_t *)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HRESULT hr = au->pClient->lpVtbl->Start(au->pClient);
    if (!SUCCEEDED(hr)) {
        fprintf(stderr, "[RED] [win-audio] WASAPI Start failed: 0x%08lx\n", hr);
        fflush(stderr);
        return 1;
    }

    uint32_t out_rate = au->pMixFormat->nSamplesPerSec;
    uint32_t out_channels = au->pMixFormat->nChannels;
    int is_float = (au->pMixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                   (au->pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                    au->pMixFormat->wBitsPerSample == 32);

    while (au->running) {
        WaitForSingleObject(au->h_event, 20);
        if (!au->running) break;

        UINT32 padding = 0;
        hr = au->pClient->lpVtbl->GetCurrentPadding(au->pClient, &padding);
        if (!SUCCEEDED(hr)) continue;

        UINT32 avail = au->buffer_frames > padding ? au->buffer_frames - padding : 0;
        // Each render call generates WIN_AUDIO_FRAMES (240) of 48 kHz
        // Map to output frames
        UINT32 out_chunk = (UINT32)((uint64_t)WIN_AUDIO_FRAMES * out_rate / 48000);
        if (out_chunk < 1) out_chunk = WIN_AUDIO_FRAMES;

        while (avail >= out_chunk && au->running) {
            AudioBufferList bl;
            bl.mNumberBuffers = 1;
            bl.mBuffers[0].mNumberChannels = au->guest_channels;
            bl.mBuffers[0].mDataByteSize = au->pcm_scratch_bytes;
            bl.mBuffers[0].mData = au->pcm_scratch;

            AudioTimeStamp ts = {0};
            AudioUnitRenderActionFlags flags = 0;
            if (au->cb) {
                au->cb(au->cb_ref, &flags, &ts, 0, WIN_AUDIO_FRAMES, &bl);
            } else {
                memset(au->pcm_scratch, 0, au->pcm_scratch_bytes);
            }

            BYTE *pDst = NULL;
            hr = au->pRender->lpVtbl->GetBuffer(au->pRender, out_chunk, &pDst);
            if (SUCCEEDED(hr) && pDst) {
                if (is_float) {
                    float *fDst = (float *)pDst;
                    for (UINT32 i = 0; i < out_chunk; i++) {
                        // Nearest-neighbor / linear resample from 48 kHz
                        UINT32 src_frame = (UINT32)((uint64_t)i * 48000 / out_rate);
                        if (src_frame >= WIN_AUDIO_FRAMES) src_frame = WIN_AUDIO_FRAMES - 1;
                        int16_t l = au->pcm_scratch[src_frame * 2 + 0];
                        int16_t r = au->pcm_scratch[src_frame * 2 + 1];
                        float fl = (float)l / 32768.0f;
                        float fr = (float)r / 32768.0f;
                        fDst[i * out_channels + 0] = fl;
                        if (out_channels > 1) fDst[i * out_channels + 1] = fr;
                        for (UINT32 ch = 2; ch < out_channels; ch++) fDst[i * out_channels + ch] = 0.0f;
                    }
                } else {
                    int16_t *sDst = (int16_t *)pDst;
                    for (UINT32 i = 0; i < out_chunk; i++) {
                        UINT32 src_frame = (UINT32)((uint64_t)i * 48000 / out_rate);
                        if (src_frame >= WIN_AUDIO_FRAMES) src_frame = WIN_AUDIO_FRAMES - 1;
                        int16_t l = au->pcm_scratch[src_frame * 2 + 0];
                        int16_t r = au->pcm_scratch[src_frame * 2 + 1];
                        sDst[i * out_channels + 0] = l;
                        if (out_channels > 1) sDst[i * out_channels + 1] = r;
                        for (UINT32 ch = 2; ch < out_channels; ch++) sDst[i * out_channels + ch] = 0;
                    }
                }
                au->pRender->lpVtbl->ReleaseBuffer(au->pRender, out_chunk, 0);
            }
            avail -= out_chunk;
        }
    }

    au->pClient->lpVtbl->Stop(au->pClient);
    return 0;
}

AudioComponent AudioComponentFindNext(AudioComponent inComponent, const AudioComponentDescription *inDesc) {
    (void)inComponent; (void)inDesc;
    return (AudioComponent)(uintptr_t)0x1;
}

OSStatus AudioComponentInstanceNew(AudioComponent inComponent, AudioUnit *outInstance) {
    (void)inComponent;
    if (!outInstance) return -1;
    win_audio_unit_t *au = (win_audio_unit_t *)calloc(1, sizeof(win_audio_unit_t));
    if (!au) return -1;
    au->guest_rate = 48000;
    au->guest_channels = 2;
    *outInstance = (AudioUnit)au;
    return 0;
}

OSStatus AudioUnitSetProperty(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, const void *inData, UInt32 inDataSize) {
    (void)inScope; (void)inElement;
    win_audio_unit_t *au = (win_audio_unit_t *)inUnit;
    if (!au) return -1;
    if (inID == kAudioUnitProperty_StreamFormat && inData && inDataSize >= sizeof(AudioStreamBasicDescription)) {
        const AudioStreamBasicDescription *asbd = (const AudioStreamBasicDescription *)inData;
        au->guest_rate = (uint32_t)asbd->mSampleRate;
        au->guest_channels = asbd->mChannelsPerFrame;
        return 0;
    }
    if (inID == kAudioUnitProperty_SetRenderCallback && inData && inDataSize >= sizeof(AURenderCallbackStruct)) {
        const AURenderCallbackStruct *rc = (const AURenderCallbackStruct *)inData;
        au->cb = rc->inputProc;
        au->cb_ref = rc->inputProcRefCon;
        return 0;
    }
    return 0;
}

OSStatus AudioUnitInitialize(AudioUnit inUnit) {
    win_audio_unit_t *au = (win_audio_unit_t *)inUnit;
    if (!au) return -1;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HRESULT hr = CoCreateInstance(&my_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &my_IID_IMMDeviceEnumerator, (void **)&au->pEnum);
    if (!SUCCEEDED(hr) || !au->pEnum) {
        fprintf(stderr, "[RED] [win-audio] CoCreateInstance MMDeviceEnumerator failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    hr = au->pEnum->lpVtbl->GetDefaultAudioEndpoint(au->pEnum, eRender, eConsole, &au->pDev);
    if (!SUCCEEDED(hr) || !au->pDev) {
        fprintf(stderr, "[RED] [win-audio] GetDefaultAudioEndpoint failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    hr = au->pDev->lpVtbl->Activate(au->pDev, &my_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&au->pClient);
    if (!SUCCEEDED(hr) || !au->pClient) {
        fprintf(stderr, "[RED] [win-audio] Activate IAudioClient failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    hr = au->pClient->lpVtbl->GetMixFormat(au->pClient, &au->pMixFormat);
    if (!SUCCEEDED(hr) || !au->pMixFormat) {
        fprintf(stderr, "[RED] [win-audio] GetMixFormat failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    fprintf(stderr, "[win-audio] WASAPI Native Mix Format: %lu Hz, %u channels, %u bits/sample\n",
            au->pMixFormat->nSamplesPerSec, au->pMixFormat->nChannels, au->pMixFormat->wBitsPerSample);
    fflush(stderr);

    au->h_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!au->h_event) return -1;

    // Buffer duration 50 ms (500,000 in 100ns units)
    REFERENCE_TIME dur = 500000;
    hr = au->pClient->lpVtbl->Initialize(au->pClient, AUDCLNT_SHAREMODE_SHARED,
                                         AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                         dur, 0, au->pMixFormat, NULL);
    if (!SUCCEEDED(hr)) {
        fprintf(stderr, "[RED] [win-audio] IAudioClient Initialize failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    hr = au->pClient->lpVtbl->SetEventHandle(au->pClient, au->h_event);
    if (!SUCCEEDED(hr)) {
        fprintf(stderr, "[RED] [win-audio] SetEventHandle failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    hr = au->pClient->lpVtbl->GetService(au->pClient, &my_IID_IAudioRenderClient, (void **)&au->pRender);
    if (!SUCCEEDED(hr) || !au->pRender) {
        fprintf(stderr, "[RED] [win-audio] GetService IAudioRenderClient failed: 0x%08lx\n", hr);
        fflush(stderr);
        return -1;
    }

    au->pClient->lpVtbl->GetBufferSize(au->pClient, &au->buffer_frames);

    au->pcm_scratch_bytes = WIN_AUDIO_FRAMES * au->guest_channels * sizeof(int16_t);
    au->pcm_scratch = (int16_t *)calloc(1, au->pcm_scratch_bytes);

    return 0;
}

OSStatus AudioOutputUnitStart(AudioUnit ci) {
    win_audio_unit_t *au = (win_audio_unit_t *)ci;
    if (!au || !au->pClient) return -1;
    if (au->running) return 0;
    au->running = 1;
    au->h_thread = CreateThread(NULL, 0, win_wasapi_thread, au, 0, NULL);
    return au->h_thread ? 0 : -1;
}

OSStatus AudioOutputUnitStop(AudioUnit ci) {
    win_audio_unit_t *au = (win_audio_unit_t *)ci;
    if (!au) return 0;
    if (au->running) {
        au->running = 0;
        if (au->h_event) SetEvent(au->h_event);
        if (au->h_thread) {
            WaitForSingleObject(au->h_thread, 1000);
            CloseHandle(au->h_thread);
            au->h_thread = NULL;
        }
    }
    return 0;
}

OSStatus AudioUnitUninitialize(AudioUnit inUnit) {
    win_audio_unit_t *au = (win_audio_unit_t *)inUnit;
    if (!au) return 0;
    AudioOutputUnitStop(inUnit);

    if (au->pRender) { au->pRender->lpVtbl->Release(au->pRender); au->pRender = NULL; }
    if (au->pClient) { au->pClient->lpVtbl->Release(au->pClient); au->pClient = NULL; }
    if (au->pDev) { au->pDev->lpVtbl->Release(au->pDev); au->pDev = NULL; }
    if (au->pEnum) { au->pEnum->lpVtbl->Release(au->pEnum); au->pEnum = NULL; }
    if (au->pMixFormat) { CoTaskMemFree(au->pMixFormat); au->pMixFormat = NULL; }
    if (au->h_event) { CloseHandle(au->h_event); au->h_event = NULL; }
    free(au->pcm_scratch);
    au->pcm_scratch = NULL;

    CoUninitialize();
    return 0;
}

OSStatus AudioComponentInstanceDispose(AudioUnit inInstance) {
    if (!inInstance) return 0;
    AudioUnitUninitialize(inInstance);
    free(inInstance);
    return 0;
}

OSStatus AudioUnitGetProperty(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void *outData, UInt32 *ioDataSize) {
    (void)inUnit; (void)inID; (void)inScope; (void)inElement; (void)outData; (void)ioDataSize;
    return -1;
}

OSStatus AudioUnitAddPropertyListener(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void *inProcUserData) {
    (void)inUnit; (void)inID; (void)inProc; (void)inProcUserData;
    return 0;
}

OSStatus AudioUnitRemovePropertyListenerWithUserData(AudioUnit inUnit, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void *inProcUserData) {
    (void)inUnit; (void)inID; (void)inProc; (void)inProcUserData;
    return 0;
}
