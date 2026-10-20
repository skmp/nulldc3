/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "oslib/audiostream.h"
#if HOST_OS==OS_WINDOWS
#include "oslib.h"
#include <initguid.h>
#include <dsound.h>

#define SOUND_BUFFER_SIZE 8192

IDirectSound8* dsound;
IDirectSoundBuffer8* buffer;

static HANDLE *buffer_events;
static int num_buffer_events = 0;
static bool dsound_running = false;
static HANDLE thread;
static DWORD tid;
static int chunk_size;
static u32 writeCursor;
static u32 bufferSize;

static audio_backend_pull_callback_t pull_callback;

DWORD CALLBACK dsound_thread(PVOID param)
{
	while (dsound_running) {
		u32 rv = WaitForMultipleObjects(num_buffer_events, buffer_events, FALSE, 20);
		if (rv == WAIT_OBJECT_0)
			return 0;

		void *p1, *p2;
		DWORD s1, s2;

		DWORD read, usedBuffer, freeBuffer;

		verifyc(buffer->GetCurrentPosition(&read, NULL));
		if (writeCursor > read)
			usedBuffer = writeCursor - read;
		else
			usedBuffer = bufferSize - (read - writeCursor);

		freeBuffer = bufferSize - usedBuffer;

		while (freeBuffer >= chunk_size)
		{
			verifyc(buffer->Lock(writeCursor, chunk_size, &p1, &s1, &p2, &s2, 0));
			{
				if (pull_callback(nullptr, 0, 0, 0) >= 512)
				{
					pull_callback(p1, s1 / 4, s1 / 4, 0);
					if (p2 != nullptr)
						pull_callback(p2, s2 / 4, s2 / 4, 0);
				}
				else {
					memset(p1, 0, s1);
					if (p2 != nullptr)
						memset(p2, 0, s2);
				}
			}
			buffer->Unlock(p1, s1, p2, s2);
			writeCursor = (writeCursor + chunk_size) % bufferSize;
			freeBuffer -= chunk_size;
		}
	}
	return 0;
}

// A failure of DirectSound is said once on stdout, and the run goes on without sound (never a fatal end)
static bool directsound_failed(const char* what, HRESULT hr)
{
	if (SUCCEEDED(hr))
		return false;
	printf("DirectSound: %s failed (0x%08lx): no sound\n", what, (unsigned long)hr);
	return true;
}

static void directsound_init(audio_backend_pull_callback_t pull_callback)
{
	::pull_callback = pull_callback;
	dsound = nullptr;
	buffer = nullptr;
	thread = nullptr;
	buffer_events = nullptr;
	num_buffer_events = 0;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// The test hook NULLDC_TEST_DSOUND=fail (tests/windows, windows.dsound.failed): a system without a sound device,
	// whose DirectSoundCreate8 answers DSERR_NODRIVER; DirectSound is not called, so nothing sounds
	const char* test_hook = getenv("NULLDC_TEST_DSOUND");
	bool forced_failure = test_hook != nullptr && strcmp(test_hook, "fail") == 0;
	if (directsound_failed("DirectSoundCreate8", forced_failure ? DSERR_NODRIVER : DirectSoundCreate8(NULL, &dsound, NULL)))
	{
		dsound = nullptr;
		return;
	}

	// DirectSound wants a top-level window: the emulator's, or the desktop when there is none (headless)
	HWND window = (HWND)libPvr_GetRenderTarget();
	if (window == NULL)
		window = GetDesktopWindow();
	if (directsound_failed("SetCooperativeLevel", dsound->SetCooperativeLevel(window, DSSCL_PRIORITY)))
	{
		dsound->Release();
		dsound = nullptr;
		return;
	}
	IDirectSoundBuffer* buffer_;

	WAVEFORMATEX wfx;
	DSBUFFERDESC desc;

	// Set up WAV format structure.

	memset(&wfx, 0, sizeof(WAVEFORMATEX));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = 44100;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.wBitsPerSample = 16;

	// Set up DSBUFFERDESC structure.

	bufferSize = SOUND_BUFFER_SIZE * wfx.nBlockAlign;

	memset(&desc, 0, sizeof(DSBUFFERDESC));
	desc.dwSize = sizeof(DSBUFFERDESC);
	desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY;// _CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;

	desc.dwBufferBytes = bufferSize;
	desc.lpwfxFormat = &wfx;

	if (settings.aica.HW_mixing == 0)
	{
		desc.dwFlags |= DSBCAPS_LOCSOFTWARE;
	}
	else if (settings.aica.HW_mixing == 1)
	{
		desc.dwFlags |= DSBCAPS_LOCHARDWARE;
	}
	else if (settings.aica.HW_mixing == 2)
	{
		//auto
	}
	else
	{
		die("settings.HW_mixing: Invalid value");
	}

	if (settings.aica.GlobalFocus)
		desc.dwFlags |= DSBCAPS_GLOBALFOCUS;

	if (directsound_failed("CreateSoundBuffer", dsound->CreateSoundBuffer(&desc, &buffer_, 0)))
	{
		dsound->Release();
		dsound = nullptr;
		return;
	}
	HRESULT hr = buffer_->QueryInterface(IID_IDirectSoundBuffer8, (void**)& buffer);
	buffer_->Release();
	if (directsound_failed("QueryInterface(IDirectSoundBuffer8)", hr))
	{
		buffer = nullptr;
		dsound->Release();
		dsound = nullptr;
		return;
	}

	int num_buffers = (bufferSize / wfx.nBlockAlign) / 512;
	chunk_size = 512 * wfx.nBlockAlign;
	num_buffer_events = num_buffers + 1;
	buffer_events = new HANDLE[num_buffer_events];
	DSBPOSITIONNOTIFY* notfy = new DSBPOSITIONNOTIFY[num_buffers];
	for (int i = 0; i < num_buffers; i++)
	{
		notfy[i].dwOffset = chunk_size * i;
		notfy[i].hEventNotify = buffer_events[i+1] = CreateEvent(NULL, FALSE, FALSE, NULL);
	}
	buffer_events[0] = CreateEvent(NULL, FALSE, FALSE, NULL);

	IDirectSoundNotify* buffer_notify;
	hr = buffer->QueryInterface(IID_IDirectSoundNotify, (void**)& buffer_notify);
	if (SUCCEEDED(hr))
	{
		hr = buffer_notify->SetNotificationPositions(num_buffers, notfy);
		buffer_notify->Release();
	}
	delete[] notfy;

	//Play the buffer !
	if (directsound_failed("the notification positions", hr) || directsound_failed("Play", buffer->Play(0, 0, DSBPLAY_LOOPING)))
	{
		for (int i = 0; i < num_buffer_events; i++)
			CloseHandle(buffer_events[i]);
		delete[] buffer_events;
		buffer_events = nullptr;
		num_buffer_events = 0;
		buffer->Release();
		buffer = nullptr;
		dsound->Release();
		dsound = nullptr;
		return;
	}

	dsound_running = true;
	if (thread = ::CreateThread(nullptr, 0, dsound_thread, nullptr, 0, &tid))
	{
		::SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
	}
}

static u32 directsound_push(void* frame, u32 samples, bool wait)
{
	return 0;
}

static void directsound_term()
{
	if (!dsound)
		return;	// it never started: nothing to end

	if (buffer_events)
		SetEvent(buffer_events[0]);
	dsound_running = false;
	if (thread)
	{
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
		thread = nullptr;
	}

	if (buffer)
	{
		buffer->Stop();
		buffer->Release();
		buffer = nullptr;
	}
	dsound->Release();
	dsound = nullptr;

	for (int i = 0; i < num_buffer_events; i++)
		CloseHandle(buffer_events[i]);
	delete[] buffer_events;
	buffer_events = nullptr;
	num_buffer_events = 0;
}

// Pull only while the thread that pulls runs: a DirectSound that failed (or whose thread did not start) is no sound,
// and the stream then takes the push path, whose directsound_push takes nothing and waits for nobody. In pull mode
// with the speed limit (aica.LimitFPS) the emulator waits for room in the ring, which only dsound_thread makes.
static bool directsound_preferpull()
{
	return dsound_running && thread != nullptr;
}

audiobackend_t audiobackend_directsound = {
    "directsound", // Slug
    "Microsoft DirectSound", // Name
    &directsound_init,
    &directsound_push,
    &directsound_term,
	NULL,
	&directsound_preferpull
};

static bool ds = RegisterAudioBackend(&audiobackend_directsound);
#endif
