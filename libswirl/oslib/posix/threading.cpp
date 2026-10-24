/*
	This file is part of libswirl
*/
#include "license/bsd"

#include "oslib/threading.h"
#include "build.h"
#include <unistd.h>

// Thread & related platform dependant code
#if !defined(HOST_NO_THREADS)
void cThread::Start() {
	hThread = new pthread_t;
#if HOST_OS == OS_DARWIN
	// the stack of a thread on Linux, 8 MiB: a thread that macOS makes without one gets 512 KiB (PL-D48)
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
	pthread_create( hThread, &attr, entry, param);
	pthread_attr_destroy(&attr);
#else
	pthread_create( hThread, NULL, entry, param);
#endif
}
void cThread::WaitToEnd() {
	if (hThread) {
		pthread_join(*hThread,0);
		delete hThread;
		hThread = NULL;
	}
}
#endif

cMutex::cMutex() {
	pthread_mutex_init(&mutx, NULL);
}
cMutex::~cMutex() {
	pthread_mutex_destroy(&mutx);
}
void cMutex::Lock() {
	pthread_mutex_lock(&mutx);
}
bool cMutex::TryLock() {
	return pthread_mutex_trylock(&mutx)==0;
}
void cMutex::Unlock() {
	pthread_mutex_unlock(&mutx);
}

void SleepMs(unsigned count) {
	usleep(count * 1000);
}

cResetEvent::cResetEvent() {
	pthread_mutex_init(&mutx, NULL);
	pthread_cond_init(&cond, NULL);
	state = false;	// not signalled: the first Wait() waits for a Set()
}
cResetEvent::~cResetEvent() {
}
void cResetEvent::Set()//Signal
{
	pthread_mutex_lock( &mutx );
	state=true;
    pthread_cond_signal( &cond);
	pthread_mutex_unlock( &mutx );
}
void cResetEvent::Reset()//reset
{
	pthread_mutex_lock( &mutx );
	state=false;
	pthread_mutex_unlock( &mutx );
}
bool cResetEvent::Wait(unsigned msec)//Wait for signal , then reset
{
	pthread_mutex_lock( &mutx );
	if (!state)
	{
		struct timespec ts;
		#if HOST_OS == OS_DARWIN
			// OSX doesn't have clock_gettime.
			clock_serv_t cclock;
			mach_timespec_t mts;

			host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
			clock_get_time(cclock, &mts);
			mach_port_deallocate(mach_task_self(), cclock);
			ts.tv_sec = mts.tv_sec;
			ts.tv_nsec = mts.tv_nsec;
		#else
			clock_gettime(CLOCK_REALTIME, &ts);
		#endif
		ts.tv_sec += msec / 1000;
		ts.tv_nsec += (msec % 1000) * 1000000;
		while (ts.tv_nsec > 1000000000)
		{
			ts.tv_nsec -= 1000000000;
			ts.tv_sec++;
		}
		pthread_cond_timedwait( &cond, &mutx, &ts );
	}
	bool rc = state;
	state=false;
	pthread_mutex_unlock( &mutx );

	return rc;
}
void cResetEvent::Wait()//Wait for signal , then reset
{
	pthread_mutex_lock( &mutx );
	// pthread_cond_wait may return without a signal: only a Set() ends the wait
	while (!state)
	{
		pthread_cond_wait( &cond, &mutx );
	}
	state=false;
	pthread_mutex_unlock( &mutx );
}


