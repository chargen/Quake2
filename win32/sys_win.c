/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sys_win.h

#include <direct.h>
#include <excpt.h>
#include "winquake.h"
#include "conproc.h"

#include "../qcommon/qcommon.h"
#include "../client/client.h"

#define MINIMUM_WIN_MEMORY	0x0a00000
#define MAXIMUM_WIN_MEMORY	0x1000000

//#define DEMO

qboolean s_win95;

int			starttime;
bool		ActiveApp, Minimized;

static HANDLE		hinput, houtput;

unsigned	sys_msg_time;
unsigned	sys_frame_time;


/*
===============================================================================

SYSTEM IO

===============================================================================
*/


void Sys_Quit (void)
{
	timeEndPeriod (1);

	CL_Shutdown (false);
	QCommon_Shutdown ();
	if (dedicated && dedicated->integer)
		FreeConsole ();

	// shut down QHOST hooks if necessary
//??	DeinitConProc ();

	exit (0);
}


void WinError (void)
{
	LPVOID lpMsgBuf;

	FormatMessage (
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		GetLastError (),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL
	);

	// Display the string.
	MessageBox (NULL, lpMsgBuf, "GetLastError", MB_OK|MB_ICONINFORMATION);

	// Free the buffer.
	LocalFree (lpMsgBuf);
}

//================================================================


/*
================
Sys_ScanForCD

================
*/
static char *Sys_ScanForCD (void)
{
	static char	cddir[MAX_OSPATH];
	static qboolean	done;
#ifdef CD_PATH
#ifndef DEMO
	static char drive[4] = "c:/";
	FILE		*f;

	if (done)		// don't re-check
		return cddir;

	// no abort/retry/fail errors
	SetErrorMode (SEM_FAILCRITICALERRORS);

	done = true;

	// scan the drives
	for (drive[0] = 'c'; drive[0] <= 'z'; drive[0]++)
	{
		if (GetDriveType (drive) != DRIVE_CDROM) continue;

		Q_CopyFilename (cddir, va("%s"CD_PATH, drive), sizeof(cddir));
		f = fopen (va("%s"CD_CHECK, drive), "r");
		if (f)
		{
			fclose (f);
			return cddir;
		}
	}
#endif
#endif

	cddir[0] = 0;
	return cddir;
}

/*
================
Sys_CopyProtect

================
*/
void	Sys_CopyProtect (void)
{
#ifdef CD_PATH
#ifndef DEMO
	char	*cddir;

	cddir = Sys_ScanForCD ();
	if (!cddir[0])
		Com_Error (ERR_FATAL, "You must have the "APPNAME" CD in the drive to play.");
#endif
#endif
}


//================================================================

/* NOTE: If error happens in ref_xxx.dll, error message will contain reference to
 * <not available> module (check this ??)
 */

extern qboolean debugLogged;
char GErrorHistory[4096];
static bool swError;

typedef unsigned address_t;


void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	swError = true;
	Com_sprintf (ARRAY_ARG(GErrorHistory), "Error: %s\n\nHistory: ", text);

	*((int*)NULL) = 0; // throw 1;
}


// from new Macro.h
#define FIELD_OFS(struc, field)		((unsigned) &((struc *)NULL)->field)		// get offset of the field in struc
#define OFS_FIELD(struc, ofs, type)	(*(type*) ((byte*)(struc) + ofs))			// get field of type by offset inside struc

// from DbgSymbols[Win32].cpp (slightly modified)

static char		module[256];
static HMODULE	hModule;

bool osAddressInfo (address_t address, char *pkgName, int bufSize, int *offset)
{
	MEMORY_BASIC_INFORMATION mbi;
	char	*s;

	hModule = NULL;
	if (!VirtualQuery ((void*) address, &mbi, sizeof(mbi)))
		return false;
	if (!(hModule = (HMODULE)mbi.AllocationBase))
		return false;
	if (!GetModuleFileName (hModule, ARRAY_ARG(module)))
		return false;

	if (s = strrchr (module, '.')) *s = 0;
	if (!(s = strrchr (module, '\\')))
		s = module;
	else
		strcpy (module, s+1);	// remove "path\" part

	*offset = address - (unsigned)hModule;
	Q_strncpyz (pkgName, module, bufSize);

	return true;
}

#define OffsetPointer(p, expr)  ((int)(p) + expr)

bool osModuleInfo (address_t address, char *exportName, int bufSize, int *offset)
{
	char	func[256];

	if (!hModule) return false;		// there was no osAddressInfo() call before this

	__try
	{
		WORD	*tmp;
		DWORD	*addrTbl, *nameTbl;
		IMAGE_OPTIONAL_HEADER32 *hdr;
		IMAGE_EXPORT_DIRECTORY* exp;
		int		i;
		unsigned bestRVA = 0;
		int		bestIndex = -1;
		unsigned RVA = address - (unsigned)hModule;

		// we want to walk system memory -- not very safe action
		if (*(WORD*)hModule != 'M'+('Z'<<8)) return false;		// bad MZ header
		tmp = (WORD*) (*(DWORD*) OffsetPointer (hModule, 0x3C) + (char*)hModule);
		if (*tmp != 'P'+('E'<<8)) return false;					// non-PE executable
		hdr = (IMAGE_OPTIONAL_HEADER32*) OffsetPointer (tmp, 4 + sizeof(IMAGE_FILE_HEADER));
		// check export directory
		if (hdr->DataDirectory[0].VirtualAddress == 0 || hdr->DataDirectory[0].Size == 0)
			return false;
		exp = (IMAGE_EXPORT_DIRECTORY*) OffsetPointer (hModule, hdr->DataDirectory[0].VirtualAddress);

		addrTbl = (DWORD*) OffsetPointer (hModule, exp->AddressOfFunctions);
		nameTbl = (DWORD*) OffsetPointer (hModule, exp->AddressOfNames);
		for (i = 0; i < exp->NumberOfFunctions; i++)
		{
			if (addrTbl[i] <= RVA && addrTbl[i] > bestRVA)
			{
				bestRVA = addrTbl[i];
				bestIndex = i;
			}
		}
		if (bestIndex >= 0)
		{
			if (nameTbl[bestIndex])
				Q_strncpyz (func, (char*) OffsetPointer (hModule, nameTbl[bestIndex]), sizeof(func));
			else
				Com_sprintf (ARRAY_ARG(func), "#%d", exp->Base +		// ordinal base
					((WORD*) OffsetPointer (hModule, exp->AddressOfNameOrdinals))[bestIndex]);
			*offset = RVA - bestRVA;
		}
		else
			return false;			// when 0 < RVA < firstFuncRVA (should not happens)
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}

	Com_sprintf (exportName, bufSize, "%s!%s", module, func);

	hModule = NULL;					// disallow second subsequentioal call
	return true;
}

bool appSymbolName2 (address_t addr, char *buffer, int size)
{
	char	package[256], *name;
	int		offset;

	if (!osAddressInfo (addr, ARRAY_ARG(package), &offset))
		return false;

	if (osModuleInfo (addr, ARRAY_ARG(package), &offset))
		name = package;

	// package + offset
	name = package;

	if (offset)
		Com_sprintf (buffer, size, "%s+%X", name, offset);
	else
		Q_strncpyz (buffer, name, size);

	return true;
}

char *appSymbolName (address_t addr)
{
	static char	buf[256];

	if (appSymbolName2 (addr, ARRAY_ARG(buf)))
		return buf;
	else
		return va("%08X", addr);
}

// from ExceptFilterWin32.cpp (slightly modified)

#define LOG_STRINGS
#define STACK_UNWIND_DEPTH		80


#define MIN_STRING_WIDTH		3
#define MAX_STRING_WIDTH		32


static void DumpReg4 (FILE *f, char *name, unsigned value)
{
	char	*data;
	int		i;

	data = (char*) value;
	fprintf (f, "  %s: %08X  ", name, value);
	if (IsBadReadPtr (data, 16))
		fprintf (f, " <N/A>");
	else
	{
		for (i = 0; i < 16; i++)
			fprintf (f, " %02X", data[i] & 0xFF);

		fprintf (f, "  ");

		for (i = 0; i < 16; i++)
		{
			char c;

			c = data[i];
			if (c < ' ' || c > 0x7F) c = '.';
			fprintf (f, "%c", c);
		}
	}
	fprintf (f, "\n");
}


static void DumpReg2 (FILE *f, char *name, DWORD value)
{
	fprintf (f, "  %s: %04X", name, value);
}


#ifdef LOG_STRINGS

static bool IsString (char *str)
{
	int		i;
	char	c;
	for (i = 0; i < MAX_STRING_WIDTH; i++, str++)
	{
		if (IsBadReadPtr (str, 1)) return false;

		c = *str;
		if (c == 0) return i >= MIN_STRING_WIDTH;
		if ((c < 32 || c > 127) && c != '\n') return false;
	}
	return true;
}


static bool DumpString (FILE *f, char *str)
{
	int		i;

	fprintf (f, " = \"");
	for (i = 0; i < MAX_STRING_WIDTH && *str; i++, str++)
	{
		if (*str == '\n')
			fprintf (f, "\\n");
		else
			fprintf (f, "%c", *str);
	}
	fprintf (f, "%s\"", *str ? "..." : "");
	return true;
}

#endif


static void DumpMem (FILE *f, unsigned *data, CONTEXT *ctx)
{
	static struct {
		unsigned ofs;
		const char *name;
	} regData[] = {
#define F(r,n)	{ FIELD_OFS(CONTEXT,r), n }
		F(Eip, "EIP"), F(Esp, "ESP"), F(Ebp, "EBP"),
		F(Eax, "EAX"), F(Ebx, "EBX"), F(Ecx, "ECX"), F(Edx, "EDX"),
		F(Esi, "ESI"), F(Edi, "EDI")
#undef F
	};
#define STAT_SPACES		"     "
	int		i, j, n;

	//!! should try to use address as a symbol
	n = 0;
	for (i = 0; i < STACK_UNWIND_DEPTH; i++, data++)
	{
		char	symbol[256], *spaces;

		if (IsBadReadPtr (data, 4))
		{
			fprintf (f, "  <N/A>\n");
			return;
		}

		spaces = STAT_SPACES;
		for (j = 0; j < ARRAY_COUNT(regData); j++)
			if (OFS_FIELD(ctx, regData[j].ofs, unsigned*) == data)
			{
				if (n)
				{
					fprintf (f, "\n");
					n = 0;
				}
				fprintf (f, "%s->", regData[j].name);
				spaces = "";
				break;
			}

		if (appSymbolName2 (*data, ARRAY_ARG(symbol)))
#ifdef LOG_FUNCS_ONLY
			if (strchr (symbol, '('))
#endif
				{
					// log as symbol
					fprintf (f, "%s%s%08X = %s",
						n > 0 ? "\n" : "", spaces,
						*data, symbol);
#if !defined(LOG_FUNCS_ONLY) && defined(LOG_STRINGS)
					if (!strchr (symbol, '(') && IsString ((char*)*data))	// do not test funcs()
						DumpString (f, (char*)*data);
#endif
					fprintf (f, "\n");
					n = 0;
					continue;
				}

#ifdef LOG_STRINGS
		// try to log as string
		if (IsString ((char*)*data))
		{
			fprintf (f, "%s%08X", n > 0 ? "\n" STAT_SPACES : spaces, *data);
			DumpString (f, (char*)*data);
			fprintf (f, "\n");
			n = 0;
			continue;
		}
#endif

		// log as simple number
		fprintf (f, "%s%08X", n > 0 ? "  " : spaces, *data);
		if (++n >= 8)
		{
			fprintf (f, "\n");
			n = 0;
		}
	}
	fprintf (f, "\n");
	return;
#undef STAT_SPACES
}


int win32ExceptFilter (struct _EXCEPTION_POINTERS *info)
{
	static bool disable = false;
	FILE	*f;

	if (disable) return EXCEPTION_EXECUTE_HANDLER;			// error will be handled only once
	disable = true;
	if (swError) return EXCEPTION_EXECUTE_HANDLER;			// no interest to thread context when software-generated errors

	__try
	{
		char	*excName;

		switch (info->ExceptionRecord->ExceptionCode)
		{
		case EXCEPTION_ACCESS_VIOLATION:
			excName = "Access violation";
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			excName = "Float zero divide";
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			excName = "Integer zero divide";
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			excName = "Priveleged instruction";
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			excName = "Illegal opcode";
			break;
		case EXCEPTION_STACK_OVERFLOW:
			excName = "Stack overflow";
			break;
		default:
			excName = "Exception";
		}

		//?? should make logging a global class (implements opening/logging date/closing)
		// make a log in "crash.log"
		if (f = fopen ("crash.log", "a+"))
		{
			CONTEXT* ctx = info->ContextRecord;
//			EXCEPTION_RECORD* rec = info->ExceptionRecord;
			time_t	itime;
			char	ctime[256];

			Com_sprintf (ARRAY_ARG(GErrorHistory), "%s at \"%s\"\n", excName, appSymbolName (ctx->Eip));

			time (&itime);
			strftime (ARRAY_ARG(ctime), "%a %b %d, %Y (%H:%M:%S)", localtime (&itime));
			fprintf (f, "----- "APPNAME" crash at %s -----\n", ctime);		//!! should use main_package name instead of APPNAME
			fprintf (f, "%s\n", GErrorHistory);
			strcat (GErrorHistory, "\nHistory: ");

			DumpReg4 (f, "EAX", ctx->Eax); DumpReg4 (f, "EBX", ctx->Ebx); DumpReg4 (f, "ECX", ctx->Ecx); DumpReg4 (f, "EDX", ctx->Edx);
			DumpReg4 (f, "ESI", ctx->Esi); DumpReg4 (f, "EDI", ctx->Edi); DumpReg4 (f, "EBP", ctx->Ebp); DumpReg4 (f, "ESP", ctx->Esp);
			DumpReg4 (f, "EIP", ctx->Eip);
			DumpReg2 (f, "CS", ctx->SegCs); DumpReg2 (f, "SS", ctx->SegSs);
			DumpReg2 (f, "DS", ctx->SegDs); DumpReg2 (f, "ES", ctx->SegEs);
			DumpReg2 (f, "FS", ctx->SegFs); DumpReg2 (f, "GS", ctx->SegGs);
			fprintf (f, "  EFLAGS: %08X\n", ctx->EFlags);

			fprintf (f, "\nStack frame:\n");
			DumpMem (f, (unsigned*) ctx->Esp, ctx);
			fprintf (f, "\n");
			fclose (f);
		}
		if (debugLogged)
			DebugPrintf ("***** CRUSH *****\n");

	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// do nothing
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

__declspec(naked) int win32ExceptFilter2 (void)
{
	__asm {
		push	[ebp-0x14]
		call	win32ExceptFilter
		add		esp,4
		retn			// return value from win32ExceptFilter()
	}
}


void appUnwind (const char *fmt, ...)
{
	char	buf[512];
	va_list	argptr;
	static bool wasError = false;

	va_start (argptr, fmt);
	if (wasError)
	{
		buf[0] = ' '; buf[1] = '<'; buf[2] = '-'; buf[3] = ' ';
		vsnprintf (buf+4, sizeof(buf)-4, fmt, argptr);
	}
	else
		vsnprintf (buf, sizeof(buf), fmt, argptr);
	wasError = true;
	va_end (argptr);

	strncat (GErrorHistory, buf, sizeof(GErrorHistory));
}


void appUnwindThrow (const char *fmt, ...)
{
	char	buf[512];
	va_list	argptr;
	static bool wasError = false;

	va_start (argptr, fmt);
	if (wasError)
	{
		buf[0] = ' '; buf[1] = '<'; buf[2] = '-'; buf[3] = ' ';
		vsnprintf (buf+4, sizeof(buf)-4, fmt, argptr);
	}
	else
		vsnprintf (buf, sizeof(buf), fmt, argptr);
	wasError = true;
	va_end (argptr);

	strncat (GErrorHistory, buf, sizeof(GErrorHistory));

	*((int*)NULL) = 0;
}


/*
================
CPUID
================
*/

static bool IsMMX, IsSSE, IsRDTSC, Is3DNow;

static bool cpuid (unsigned _in, unsigned regs[4])
{
	bool	isOK;

	isOK = true;
	__asm pushad;		// save all regs
	__try
	{
		__asm {
			mov		eax,_in
			cpuid
			mov		esi,regs
			mov		[esi],eax
			mov		[esi+4],ebx
			mov		[esi+8],ecx
			mov		[esi+12],edx
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		isOK = false;
	}
	__asm popad;
	return isOK;
}


static void CheckCpuName (void)
{
	union {
		unsigned reg[3];
		char	name[3*4+1];
	} t;
	unsigned r[4];

	if (!cpuid (0, r))
		Com_Printf ("Unknown 386/486 CPU\n");
	else
	{
		t.reg[0] = r[1];
		t.reg[1] = r[3];
		t.reg[2] = r[2];
		t.name[12] = 0;		// ASCIIZ
		Com_Printf ("CPU: %s\n", t.name);
	}
}


static void CheckCpuCaps (void)
{
	unsigned r[4];

	if (!cpuid (1, r)) return;

	Com_Printf ("CPU caps: [ ");

	if (r[3] & 0x00000001)	Com_Printf ("FPU ");
	if (r[3] & 0x00800000)
	{
		Com_Printf ("MMX ");
		IsMMX = true;
	}
	if (r[3] & 0x02000000)
	{
		Com_Printf ("SSE ");
		IsSSE = true;
	}
	if (r[3] & 0x04000000)	Com_Printf ("SSE2 ");
	if (r[3] & 0x00000010)
	{
		Com_Printf ("RDTSC ");
		IsRDTSC = true;
	}

	// check extended features
	if (cpuid (0x80000000, r))
		if (r[0] >= 0x80000001)		// largest recognized extended function
			if (cpuid (0x80000001, r) && r[3] & 0x80000000)
			{
				Com_Printf ("3DNow! ");
				Is3DNow = true;
			}

	Com_Printf ("]\n");
}


__inline __int64 cycles ()
{
	__asm
	{
		rdtsc
	}
}


static void CheckCpuSpeed (void)
{
	int		tries;
	unsigned time1, time2;
//	float	tmp;
	__int64	stamp1, stamp2;
	double secPerCycle, secPerCycle1;

	if (!IsRDTSC) return;

	timeBeginPeriod (1);

	secPerCycle = 1.0;							// initial value
	for (tries = 0; tries < 4; tries++)
	{
		stamp1 = cycles ();
		time1 = timeGetTime ();
//		tmp = 0;
		while (true)
		{
			time2 = timeGetTime ();
			if (time2 - time1 > 200) break;		// 200ms enough to compute CPU speed
//			tmp = tmp + sin (time2);			//?? just waste CPU time
		}
		stamp2 = cycles ();
		secPerCycle1 = (time2 - time1) / (((double)stamp2 - (double)stamp1) * 1000);
		if (secPerCycle1 < secPerCycle) secPerCycle = secPerCycle1;

//		Com_Printf ("try #%d\n", tries);
//		Com_Printf ("stampd: %u %u   timed: %d\n", stamp2 - stamp1, time2 - time1);
//		Com_Printf ("CPU speed: %g MHz\n", 1e-6 / secPerCycle1);
	}
	Com_Printf ("CPU speed: %g MHz\n", 1e-6 / secPerCycle);

	timeEndPeriod (1);
}


/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
	OSVERSIONINFO	vinfo;

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	if (!GetVersionEx (&vinfo))
		Sys_Error ("Couldn't get OS info");

	if (vinfo.dwMajorVersion < 4)
		Sys_Error (APPNAME" requires windows version 4 or greater");
	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32s)
		Sys_Error (APPNAME" doesn't run on Win32s");
	else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		s_win95 = true;

	CheckCpuName ();
	CheckCpuCaps ();
	CheckCpuSpeed ();

	if (dedicated->integer)
	{
		if (!AllocConsole ())
			Sys_Error ("Couldn't create dedicated server console");
		hinput = GetStdHandle (STD_INPUT_HANDLE);
		houtput = GetStdHandle (STD_OUTPUT_HANDLE);

		// let QHOST hook in
//??		InitConProc (argc, argv);
	}
	//??
	timeBeginPeriod (1);
}


static char	console_text[256];
static int	console_textlen;

/*
================
Sys_ConsoleInput
================
*/
char *Sys_ConsoleInput (void)
{
	INPUT_RECORD	recs[1024];
	int		dummy;
	int		ch, numread, numevents;

	if (!dedicated || !dedicated->integer)
		return NULL;

	for ( ;; )
	{
		if (!GetNumberOfConsoleInputEvents (hinput, &numevents))
			Sys_Error ("Error getting # of console events");

		if (numevents <= 0)
			break;

		if (!ReadConsoleInput(hinput, recs, 1, &numread))
			Sys_Error ("Error reading console input");

		if (numread != 1)
			Sys_Error ("Couldn't read console input");

		if (recs[0].EventType == KEY_EVENT)
		{
			if (!recs[0].Event.KeyEvent.bKeyDown)
			{
				ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

				switch (ch)
				{
					case '\r':
						WriteFile(houtput, "\r\n", 2, &dummy, NULL);

						if (console_textlen)
						{
							console_text[console_textlen] = 0;
							console_textlen = 0;
							return console_text;
						}
						break;

					case '\b':
						if (console_textlen)
						{
							console_textlen--;
							WriteFile(houtput, "\b \b", 3, &dummy, NULL);
						}
						break;

					default:
						if (ch >= ' ')
						{
							if (console_textlen < sizeof(console_text)-2)
							{
								WriteFile(houtput, &ch, 1, &dummy, NULL);
								console_text[console_textlen] = ch;
								console_textlen++;
							}
						}

						break;

				}
			}
		}
	}

	return NULL;
}


/*
================
Sys_ConsoleOutput

Print text to the dedicated console
================
*/
void Sys_ConsoleOutput (char *string)
{
	int		dummy;
	char	text[256];

	if (!dedicated || !dedicated->integer)
		return;

	if (console_textlen)
	{
		text[0] = '\r';
		memset(&text[1], ' ', console_textlen);
		text[console_textlen+1] = '\r';
		text[console_textlen+2] = 0;
		WriteFile(houtput, text, console_textlen+2, &dummy, NULL);
	}

	WriteFile(houtput, string, strlen(string), &dummy, NULL);

	if (console_textlen)
		WriteFile(houtput, console_text, console_textlen, &dummy, NULL);
}


/*
================
Sys_SendKeyEvents

Send Key_Event calls
================
*/
void Sys_SendKeyEvents (void)
{
	MSG		msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
		if (!GetMessage (&msg, NULL, 0, 0))
			Sys_Quit ();
		sys_msg_time = msg.time;
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}

	// grab frame time
	sys_frame_time = timeGetTime();	// FIXME: should this be at start?
}



/*
================
Sys_GetClipboardData

================
*/
char *Sys_GetClipboardData( void )
{
	char *data = NULL;
	char *cliptext;

	if (OpenClipboard (NULL))
	{
		HANDLE	hClipboardData;

		if (hClipboardData = GetClipboardData (CF_TEXT))
		{
			if (cliptext = GlobalLock (hClipboardData))
			{
				data = malloc (GlobalSize (hClipboardData) + 1);
				strcpy (data, cliptext);
				GlobalUnlock (hClipboardData);
			}
		}
		CloseClipboard ();
	}
	return data;
}

/*
==============================================================================

 WINDOWS CRAP

==============================================================================
*/

/*
=================
Sys_AppActivate
=================
*/
void Sys_AppActivate (void)
{
	ShowWindow (cl_hwnd, SW_RESTORE);
	SetForegroundWindow (cl_hwnd);
}

/*
========================================================================

GAME DLL

========================================================================
*/

static HINSTANCE	game_library;

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{
	if (!FreeLibrary (game_library))
		Com_Error (ERR_FATAL, "FreeLibrary failed for game library");
	game_library = NULL;
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
void *Sys_GetGameAPI (void *parms)
{
	void	*(*GetGameAPI) (void *);
	char	name[MAX_OSPATH];
	char	*path;
	char	cwd[MAX_OSPATH];
#if defined _M_IX86
	const char *gamename = "gamex86.dll";

#ifdef NDEBUG
	const char *debugdir = "release";
#else
	const char *debugdir = "debug";
#endif

#elif defined _M_ALPHA
	const char *gamename = "gameaxp.dll";

#ifdef NDEBUG
	const char *debugdir = "releaseaxp";
#else
	const char *debugdir = "debugaxp";
#endif

#endif

	if (game_library)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI() without Sys_UnloadGame()");

	// check the current debug directory first for development purposes
	_getcwd (cwd, sizeof(cwd));
	Com_sprintf (ARRAY_ARG(name), "%s/%s/%s", cwd, debugdir, gamename);
	game_library = LoadLibrary (name);
	if (game_library)
	{
		Com_DPrintf ("LoadLibrary (%s)\n", name);
	}
	else
	{
#ifdef DEBUG
		// check the current directory for other development purposes
		Com_sprintf (ARRAY_ARG(name), "%s/%s", cwd, gamename);
		game_library = LoadLibrary (name);
		if (game_library)
		{
			Com_DPrintf ("LoadLibrary (%s)\n", name);
		}
		else
#endif
		{
			// now run through the search paths
			path = NULL;
			while (1)
			{
				path = FS_NextPath (path);
				if (!path)
					return NULL;		// couldn't find one anywhere
				Com_sprintf (ARRAY_ARG(name), "%s/%s", path, gamename);
				game_library = LoadLibrary (name);
				if (game_library)
				{
					Com_DPrintf ("LoadLibrary (%s)\n",name);
					break;
				}
			}
		}
	}

	GetGameAPI = (void *)GetProcAddress (game_library, "GetGameAPI");
	if (!GetGameAPI)
	{
		Sys_UnloadGame ();
		return NULL;
	}

	return GetGameAPI (parms);
}

//=======================================================================


/*
==================
WinMain

==================
*/
HINSTANCE	global_hInstance;

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int		time, oldtime, newtime;
	char	*cmdline;
#ifdef CD_PATH
	char	*cddir, cmdline2[1024];
#endif

	MAINLOOP_BEGIN
	{
		global_hInstance = hInstance;

		cmdline = lpCmdLine;
#ifdef CD_PATH
		// if we find the CD, add a "-cddir=xxx" command line
		cddir = Sys_ScanForCD ();
		if (cddir && cddir[0])
		{
			// add to the end of cmdline, so, if already specified - will not override option
			Com_sprintf (ARRAY_ARG(cmdline2), "%s -cddir=\"%s\"", lpCmdLine, cddir);
			cmdline = cmdline2;
		}
#endif

		QCommon_Init (cmdline);
		oldtime = Sys_Milliseconds ();

		/*--------- main window message loop ------------*/
		guard(MainLoop);
		while (1)
		{
			MSG		msg;

			if (!ActiveApp || dedicated->integer)
				Sleep (10);		//?? what about client and server in one place: will server become slower ?

			while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
			{
				if (!GetMessage (&msg, NULL, 0, 0))
					Com_Quit ();
				sys_msg_time = msg.time;
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}

			// do not allow Qcommon_Frame(0)
			while (1)
			{
				newtime = Sys_Milliseconds ();
				time = newtime - oldtime;
				if (time >= 1) break;
				Sleep (1);
			}
			QCommon_Frame (time);

			oldtime = newtime;
		}
		unguard;
	}
	MAINLOOP_CATCH
	{
		CL_Shutdown (true);
		QCommon_Shutdown ();
		MessageBox(NULL, GErrorHistory, APPNAME ": fatal error", MB_OK|MB_ICONSTOP/*|MB_TOPMOST*/|MB_SETFOREGROUND);
		// shut down QHOST hooks if necessary
//??		DeinitConProc ();
	}
	// never gets here
	return TRUE;
}
