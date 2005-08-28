#include "Core.h"

#define MAX_LOGGERS		32
#define BUFFER_LEN		4096

static int numDevices = 0;
static COutputDevice *loggers[MAX_LOGGERS];

void COutputDevice::Printf (const char *fmt, ...)
{
	va_list	argptr;
	va_start (argptr, fmt);
	char buf[BUFFER_LEN];
	int len = vsnprintf (ARRAY_ARG(buf), fmt, argptr);
	va_end (argptr);
	if (len < 0 || len >= sizeof(buf) - 1)
		return;

	Write (buf);
}


void COutputDevice::Register ()
{
	if (numDevices)
	{
		for (int i = 0; i < numDevices; i++)
			if (loggers[i] == this) return;		// already registered
		if (numDevices >= MAX_LOGGERS)
			appError ("COutputDevice::Register: too much loggers");
		// new device will be first
		memmove (loggers+1, loggers, sizeof(COutputDevice*) * numDevices);
	}
	loggers[0] = this;
	numDevices++;
}


void COutputDevice::Unregister ()
{
	for (int i = 0; i < numDevices; i++)
		if (loggers[i] == this)
		{
			// found
			numDevices--;
			if (i < numDevices)			// not last
				memcpy (loggers+i, loggers+i+1, sizeof(COutputDevice*) * (numDevices - i)); // compact list
			return;
		}
}


void appPrintf (const char *fmt, ...)
{
	guard(appPrintf);
	if (!numDevices) return;			// no loggers registered

	va_list	argptr;
	va_start (argptr, fmt);
	char buf[BUFFER_LEN];
	int len = vsnprintf (ARRAY_ARG(buf), fmt, argptr);
	va_end (argptr);
	if (len < 0 || len >= sizeof(buf) - 1) return;		//?? may be, write anyway ...

	for (int i = 0; i < numDevices; i++)
		if (!loggers[i]->Write (buf)) break;
	unguard;
}


void appWPrintf (const char *fmt, ...)
{
	va_list	argptr;
	va_start (argptr, fmt);
	char buf[BUFFER_LEN];
	int len = vsnprintf (ARRAY_ARG(buf), fmt, argptr);
	va_end (argptr);

	if (len < 0 || len >= sizeof(buf) - 1) return;		//?? may be, write anuway ...
	appPrintf (S_YELLOW"%s", buf);
//??#ifndef NO_DEVELOPER
//??	if (developer->integer == 2) DebugPrintf ("%s", msg);
//??#endif
}
