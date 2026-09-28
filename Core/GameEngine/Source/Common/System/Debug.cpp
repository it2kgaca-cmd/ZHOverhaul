/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Debug.cpp
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Debug.cpp
//
// Created:   Steven Johnson, August 2001
//
// Desc:      Debug logging and other debug utilities
//
// ----------------------------------------------------------------------------

// SYSTEM INCLUDES
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


// USER INCLUDES

// TheSuperHackers @feature helmutbuhler 04/10/2025
// Uncomment this to show normal logging stuff in the crc logging.
// This can be helpful for context, but can also clutter diffs because normal logs aren't necessarily
// deterministic or the same on all peers in multiplayer games.
//#define INCLUDE_DEBUG_LOG_IN_CRC_LOG

#define DEBUG_THREADSAFE
#ifdef DEBUG_THREADSAFE
#include "Common/CriticalSection.h"
#endif
#include "Common/CommandLine.h"
#include "Common/Debug.h"
#include "Common/CRCDebug.h"
#include "Common/UnicodeString.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#if defined(DEBUG_STACKTRACE) || defined(IG_DEBUG_STACKTRACE)
	#include "Common/StackDump.h"
#endif
#ifdef RTS_ENABLE_CRASHDUMP
#include "Common/MiniDumper.h"
#endif
#include "gitinfo.h"

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

extern const char *gAppPrefix; /// So WB can have a different log file name.


// ----------------------------------------------------------------------------
// DEFINES
// ----------------------------------------------------------------------------

#ifdef DEBUG_LOGGING

#if defined(RTS_DEBUG)
	#define DEBUG_FILE_NAME				"DebugLogFileD"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevD"
#else
	#define DEBUG_FILE_NAME				"DebugLogFile"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrev"
#endif

#endif

// ----------------------------------------------------------------------------
// PRIVATE TYPES
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// PRIVATE DATA
// ----------------------------------------------------------------------------
// TheSuperHackers @info Must not use static RAII types when set in DebugInit,
// because DebugInit can be called during static module initialization before the main function is called.
#ifdef DEBUG_LOGGING
static FILE *theLogFile = nullptr;
static char theLogFileName[ _MAX_PATH ];
static char theLogFileNamePrev[ _MAX_PATH ];
#endif
#define LARGE_BUFFER	8192
static char theBuffer[ LARGE_BUFFER ];	// make it big to avoid weird overflow bugs in debug mode
static int theDebugFlags = 0;
static DWORD theMainThreadID = 0;
// ----------------------------------------------------------------------------
// PUBLIC DATA
// ----------------------------------------------------------------------------

char* TheCurrentIgnoreCrashPtr = nullptr;
#ifdef DEBUG_LOGGING
UnsignedInt DebugLevelMask = 0;
const char *TheDebugLevels[DEBUG_LEVEL_MAX] = {
	"NET"
};
#endif

// ----------------------------------------------------------------------------
// PRIVATE PROTOTYPES
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString();
static const char *getCurrentTickString();
static void prepBuffer(char *buffer);
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer);
static void doLogOutput(const char *buffer, const char *endline);
#endif
#ifdef DEBUG_CRASHING
static int doCrashBox(const char *buffer, Bool logResult);
#endif
static void whackFunnyCharacters(char *buf);
#ifdef DEBUG_STACKTRACE
static void doStackDump();
#endif

// ----------------------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
inline Bool ignoringAsserts()
{
	if (!DX8Wrapper_IsWindowed)
		return true;
	if (TheGlobalData && TheGlobalData->m_headless)
		return true;
#ifdef DEBUG_CRASHING
	if (TheGlobalData && TheGlobalData->m_debugIgnoreAsserts)
		return true;
#endif

	return false;
}

// ----------------------------------------------------------------------------
inline HWND getThreadHWND()
{
	return (theMainThreadID == GetCurrentThreadId())?ApplicationHWnd:nullptr;
}

// ----------------------------------------------------------------------------

int MessageBoxWrapper( LPCSTR lpText, LPCSTR lpCaption, UINT uType )
{
	HWND threadHWND = getThreadHWND();
	return ::MessageBox(threadHWND, lpText, lpCaption, uType);
}

// ----------------------------------------------------------------------------
// getCurrentTimeString
/**
	Return the current time in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString()
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

// ----------------------------------------------------------------------------
// getCurrentTickString
/**
	Return the current TickCount in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTickString()
{
	static char TheTickString[32];
	snprintf(TheTickString, ARRAY_SIZE(TheTickString), "(T=%08lx)", ::GetTickCount());
	return TheTickString;
}

// ----------------------------------------------------------------------------
// prepBuffer
// zap the buffer and optionally prepend the tick time.
// ----------------------------------------------------------------------------
/**
	Empty the buffer passed in, then optionally prepend the current TickCount
	value in string form, depending on the setting of theDebugFlags.
*/
static void prepBuffer(char *buffer)
{
	buffer[0] = 0;
#ifdef ALLOW_DEBUG_UTILS
	if (theDebugFlags & DEBUG_FLAG_PREPEND_TIME)
	{
		strcpy(buffer, getCurrentTickString());
		strcat(buffer, " ");
	}
#endif
}

// ----------------------------------------------------------------------------
// doLogOutput
/**
	send a string directly to the log file and/or console without further processing.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer)
{
		doLogOutput(buffer, "\n");
}

static void doLogOutput(const char *buffer, const char *endline)
{
	// log message to file
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_FILE)
	{
		if (theLogFile)
		{
			fprintf(theLogFile, "%s%s", buffer, endline);
			fflush(theLogFile);
		}
	}

	// log message to dev studio output window
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_CONSOLE)
	{
		::OutputDebugString(buffer);
		::OutputDebugString(endline);
	}

#ifdef INCLUDE_DEBUG_LOG_IN_CRC_LOG
	addCRCDebugLineNoCounter("%s%s", buffer, endline);
#endif
}
#endif // DEBUG_LOGGING

// ----------------------------------------------------------------------------
// doCrashBox
/*
	present a messagebox with the given message. Depending on user selection,
	we exit the app, break into debugger, or continue execution.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
static int doCrashBox(const char *buffer, Bool logResult)
{
	int result;

	if (!ignoringAsserts()) {
		result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING|MB_DEFBUTTON3);
		//result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING);
	}	else {
		result = IDIGNORE;
	}

	switch(result)
	{
		case IDABORT:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Abort]");
#endif
			_exit(1);
			break;
		case IDRETRY:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Retry]");
#endif
			::DebugBreak();
			break;
		case IDIGNORE:
#ifdef DEBUG_LOGGING
			// do nothing, just keep going
			if (logResult)
				DebugLog("[Ignore]");
#endif
			break;
	}
	return result;
}
#endif

#ifdef DEBUG_STACKTRACE
// ----------------------------------------------------------------------------
/**
	Dumps a stack trace (from the current PC) to logfile and/or console.
*/
static void doStackDump()
{
	const int STACKTRACE_SIZE	= 24;
	const int STACKTRACE_SKIP = 2;
	void* stacktrace[STACKTRACE_SIZE];

	doLogOutput("\nStack Dump:");
	::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
	::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, doLogOutput);
}
#endif

// ----------------------------------------------------------------------------
// whackFunnyCharacters
/**
	Eliminates any undesirable nonprinting characters, aside from newline,
	replacing them with spaces.
*/
// ----------------------------------------------------------------------------
static void whackFunnyCharacters(char *buf)
{
	for (char *p = buf + strlen(buf) - 1; p >= buf; --p)
	{
		// ok, these are naughty magic numbers, but I'm guessing you know ASCII....
		if (*p >= 0 && *p < 32 && *p != 10 && *p != 13)
			*p = 32;
	}
}

// ----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// DebugInit
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Initialize the debug utilities. This should be called once, as near to the
	start of the app as possible, before anything else (since other code will
	probably want to make use of it).
*/
void DebugInit(int flags)
{
//	if (theDebugFlags != 0)
//		::MessageBox(nullptr, "Debug already inited", "", MB_OK|MB_APPLMODAL);

	// just quietly allow multiple calls to this, so that static ctors can call it.
	if (theDebugFlags == 0)
	{
		theDebugFlags = flags;

		theMainThreadID = GetCurrentThreadId();

	#ifdef DEBUG_LOGGING

		// TheSuperHackers @info Debug initialization can happen very early.
		// Determine the client instance id before creating the log file with an instance specific name.
		CommandLine::parseCommandLineForStartup();

		if (!rts::ClientInstance::initialize())
			return;

		char dirbuf[ _MAX_PATH ];
		::GetModuleFileName( nullptr, dirbuf, sizeof( dirbuf ) );
		if (char *pEnd = strrchr(dirbuf, '\\'))
		{
			*(pEnd + 1) = 0;
		}

		static_assert(ARRAY_SIZE(theLogFileNamePrev) >= ARRAY_SIZE(dirbuf), "Incorrect array size");
		strcpy(theLogFileNamePrev, dirbuf);
		strlcat(theLogFileNamePrev, gAppPrefix, ARRAY_SIZE(theLogFileNamePrev));
		strlcat(theLogFileNamePrev, DEBUG_FILE_NAME_PREV, ARRAY_SIZE(theLogFileNamePrev));
		if (rts::ClientInstance::getInstanceId() > 1u)
		{
			size_t offset = strlen(theLogFileNamePrev);
			snprintf(theLogFileNamePrev + offset, ARRAY_SIZE(theLogFileNamePrev) - offset, "_Instance%.2u", rts::ClientInstance::getInstanceId());
		}
		strlcat(theLogFileNamePrev, ".txt", ARRAY_SIZE(theLogFileNamePrev));

		static_assert(ARRAY_SIZE(theLogFileName) >= ARRAY_SIZE(dirbuf), "Incorrect array size");
		strcpy(theLogFileName, dirbuf);
		strlcat(theLogFileName, gAppPrefix, ARRAY_SIZE(theLogFileNamePrev));
		strlcat(theLogFileName, DEBUG_FILE_NAME, ARRAY_SIZE(theLogFileNamePrev));
		if (rts::ClientInstance::getInstanceId() > 1u)
		{
			size_t offset = strlen(theLogFileName);
			snprintf(theLogFileName + offset, ARRAY_SIZE(theLogFileName) - offset, "_Instance%.2u", rts::ClientInstance::getInstanceId());
		}
		strlcat(theLogFileName, ".txt", ARRAY_SIZE(theLogFileNamePrev));

		remove(theLogFileNamePrev);
		if (rename(theLogFileName, theLogFileNamePrev) != 0)
		{
#ifdef DEBUG_LOGGING
			DebugLog("Warning: Could not rename buffer file '%s' to '%s'. Will remove instead", theLogFileName, theLogFileNamePrev);
#endif
			if (remove(theLogFileName) != 0)
			{
#ifdef DEBUG_LOGGING
				DebugLog("Warning: Failed to remove file '%s'", theLogFileName);
#endif
			}
		}

		theLogFile = fopen(theLogFileName, "w");
		if (theLogFile != nullptr)
		{
			DebugLog("Log %s opened: %s", theLogFileName, getCurrentTimeString());
		}
	#endif
	}

}
#endif

// ----------------------------------------------------------------------------
// DebugLog
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
/**
	Print a string to the log file and/or console.
*/
void DebugLog(const char *format, ...)
{
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		MessageBoxWrapper("DebugLog - Debug not inited properly", "", MB_OK|MB_TASKMODAL);

	prepBuffer(theBuffer);

	va_list args;
	va_start(args, format);
	size_t offset = strlen(theBuffer);
	vsnprintf(theBuffer + offset, ARRAY_SIZE(theBuffer) - offset, format, args);
	va_end(args);

	if (strlen(theBuffer) >= sizeof(theBuffer))
		MessageBoxWrapper("String too long for debug buffer", "", MB_OK|MB_TASKMODAL);

	whackFunnyCharacters(theBuffer);
	doLogOutput(theBuffer);
}

/**
	Print a string with no modifications to the log file and/or console.
*/
void DebugLogRaw(const char *format, ...)
{
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		MessageBoxWrapper("DebugLogRaw - Debug not inited properly", "", MB_OK|MB_TASKMODAL);

	theBuffer[0] = 0;

	va_list args;
	va_start(args, format);
	vsnprintf(theBuffer, ARRAY_SIZE(theBuffer), format, args);
	va_end(args);

	if (strlen(theBuffer) >= sizeof(theBuffer))
		MessageBoxWrapper("String too long for debug buffer", "", MB_OK|MB_TASKMODAL);

	doLogOutput(theBuffer, "");
}

const char* DebugGetLogFileName()
{
	return theLogFileName;
}

const char* DebugGetLogFileNamePrev()
{
	return theLogFileNamePrev;
}

#endif

// ----------------------------------------------------------------------------
// DebugCrash
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
/**
	Print a character string to the log file and/or console, then halt execution
	while presenting the user with an exit/debug/ignore dialog containing the same
	text message.

	TheSuperHackers @tweak Now shows a message box without any logging when debug was not yet initialized.
*/
void DebugCrash(const char *format, ...)
{
	// Note: You might want to make this thread safe, but we cannot. The reason is that
	// there is an implicit requirement on other threads that the message loop be running.

	// make it not static so that it'll be thread-safe.
	// make it big to avoid weird overflow bugs in debug mode
	char theCrashBuffer[ LARGE_BUFFER ];

	prepBuffer(theCrashBuffer);
	strlcat(theCrashBuffer, "ASSERTION FAILURE: ", ARRAY_SIZE(theCrashBuffer));

	va_list arg;
	va_start(arg, format);
	size_t offset =  strlen(theCrashBuffer);
	vsnprintf(theCrashBuffer + offset, ARRAY_SIZE(theCrashBuffer) - offset, format, arg);
	va_end(arg);

	whackFunnyCharacters(theCrashBuffer);

	const bool useLogging = theDebugFlags != 0;

	if (useLogging)
	{
#ifdef DEBUG_LOGGING
		if (ignoringAsserts())
		{
			doLogOutput("**** CRASH IN FULL SCREEN - Auto-ignored, CHECK THIS LOG!");
		}
		doLogOutput(theCrashBuffer);
#endif
#ifdef DEBUG_STACKTRACE
		if (!(TheGlobalData && TheGlobalData->m_debugIgnoreStackTrace))
		{
			doStackDump();
		}
#endif
	}

	strlcat(theCrashBuffer, "\n\nAbort->exception; Retry->debugger; Ignore->continue", ARRAY_SIZE(theCrashBuffer));

	const int result = doCrashBox(theCrashBuffer, useLogging);

	if (result == IDIGNORE && TheCurrentIgnoreCrashPtr != nullptr)
	{
		int yn;
		if (!ignoringAsserts())
		{
			yn = MessageBoxWrapper("Ignore this crash from now on?", "", MB_YESNO|MB_TASKMODAL);
		}
		else
		{
			yn = IDYES;
		}
		if (yn == IDYES)
			*TheCurrentIgnoreCrashPtr = 1;
		if( TheKeyboard )
			TheKeyboard->resetKeys();
		if( TheMouse )
			TheMouse->reset();
	}

}
#endif

// ----------------------------------------------------------------------------
// DebugShutdown
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Shut down the debug utilities. This should be called once, as near to the
	end of the app as possible, after everything else (since other code will
	probably want to make use of it).
*/
void DebugShutdown()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		DebugLog("Log closed: %s", getCurrentTimeString());
		fclose(theLogFile);
	}
	theLogFile = nullptr;
#endif
	theDebugFlags = 0;
}

// ----------------------------------------------------------------------------
// DebugGetFlags
// ----------------------------------------------------------------------------
/**
	Get the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
int DebugGetFlags()
{
	return theDebugFlags;
}

// ----------------------------------------------------------------------------
// DebugSetFlags
// ----------------------------------------------------------------------------
/**
	Set the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
void DebugSetFlags(int flags)
{
	theDebugFlags = flags;
}

#endif	// ALLOW_DEBUG_UTILS

#ifdef DEBUG_PROFILE
// ----------------------------------------------------------------------------
SimpleProfiler::SimpleProfiler()
{
	QueryPerformanceFrequency((LARGE_INTEGER*)&m_freq);
	m_startThisSession = 0;
	m_totalThisSession = 0;
	m_totalAllSessions = 0;
	m_numSessions = 0;
}

// ----------------------------------------------------------------------------
void SimpleProfiler::start()
{
	DEBUG_ASSERTCRASH(m_startThisSession == 0, ("already started"));
	QueryPerformanceCounter((LARGE_INTEGER*)&m_startThisSession);
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stop()
{
	if (m_startThisSession != 0)
	{
		__int64 stop;
		QueryPerformanceCounter((LARGE_INTEGER*)&stop);
		m_totalThisSession = stop - m_startThisSession;
		m_totalAllSessions += stop - m_startThisSession;
		m_startThisSession = 0;
		++m_numSessions;
	}
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stopAndLog(const char *msg, int howOftenToLog, int howOftenToResetAvg)
{
	stop();
	// howOftenToResetAvg==0 means "never reset"
	if (howOftenToResetAvg > 0 && m_numSessions >= howOftenToResetAvg)
	{
		m_numSessions = 0;
		m_totalAllSessions = 0;
		DEBUG_LOG(("%s: reset averages",msg));
	}
	DEBUG_ASSERTLOG(m_numSessions % howOftenToLog != 0, ("%s: %f msec, total %f msec, avg %f msec",msg,getTime(),getTotalTime(),getAverageTime()));
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTime()
{
	stop();
	return (double)m_totalThisSession / (double)m_freq * 1000.0;
}

// ----------------------------------------------------------------------------
int SimpleProfiler::getNumSessions()
{
	stop();
	return m_numSessions;
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTotalTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq);
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getAverageTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq * (double)m_numSessions);
}

#endif	// DEBUG_PROFILE

// ----------------------------------------------------------------------------
// ReleaseCrash
// ----------------------------------------------------------------------------
/**
	Halt the application, EVEN IN FINAL RELEASE BUILDS. This should be called
	only when a crash is guaranteed by continuing, and no meaningful continuation
	of processing is possible, even by throwing an exception.
*/

	#define RELEASECRASH_FILE_NAME				"ReleaseCrashInfo.txt"
	#define RELEASECRASH_FILE_NAME_PREV		"ReleaseCrashInfoPrev.txt"

	static FILE *theReleaseCrashLogFile = nullptr;

	static void releaseCrashLogOutput(const char *buffer)
	{
		if (theReleaseCrashLogFile)
		{
			fprintf(theReleaseCrashLogFile, "%s\n", buffer);
			fflush(theReleaseCrashLogFile);
		}
	}


static void TriggerMiniDump()
{
#ifdef RTS_ENABLE_CRASHDUMP
	if (TheMiniDumper && TheMiniDumper->IsInitialized())
	{
		// Create both minimal and full memory dumps.
		TheMiniDumper->TriggerMiniDump(DumpType_Minimal);
		TheMiniDumper->TriggerMiniDump(DumpType_Full);
	}

	MiniDumper::shutdownMiniDumper();
#endif
}

// ----------------------------------------------------------------------------
// Build crash-report paths. Prefer the normal user-data directory, but fall back
// to the executable directory if a fatal error occurs before/after GlobalData exists.
// ----------------------------------------------------------------------------
static void buildReleaseCrashPaths(char *prevbuf, size_t prevCount, char *curbuf, size_t curCount)
{
	prevbuf[0] = 0;
	curbuf[0] = 0;

	if (TheGlobalData)
	{
		strlcpy(curbuf, TheGlobalData->getPath_UserData().str(), curCount);
	}
	else
	{
		::GetModuleFileName(nullptr, curbuf, static_cast<DWORD>(curCount));
		char *slash = strrchr(curbuf, '\\');
		if (slash)
			*(slash + 1) = 0;
		else
			curbuf[0] = 0;
	}

	strlcpy(prevbuf, curbuf, prevCount);
	strlcat(prevbuf, RELEASECRASH_FILE_NAME_PREV, prevCount);
	strlcat(curbuf, RELEASECRASH_FILE_NAME, curCount);
}

// ----------------------------------------------------------------------------
static void writeReleaseCrashReport(
	const char *reason,
	const char *sourceFile,
	Int sourceLine,
	const char *functionName,
	const char *prevbuf,
	const char *curbuf)
{
	remove(prevbuf);
	if (rename(curbuf, prevbuf) != 0)
	{
#ifdef DEBUG_LOGGING
		DebugLog("Warning: Could not rotate crash report '%s' to '%s'. Will replace current report.", curbuf, prevbuf);
#endif
		remove(curbuf);
	}

	theReleaseCrashLogFile = fopen(curbuf, "w");
	if (!theReleaseCrashLogFile)
		return;

	char executable[_MAX_PATH] = { 0 };
	::GetModuleFileName(nullptr, executable, ARRAY_SIZE(executable));

	fprintf(theReleaseCrashLogFile, "ZHOverhaul Crash Report\n");
	fprintf(theReleaseCrashLogFile, "Time: %s\n", getCurrentTimeString());
	fprintf(theReleaseCrashLogFile, "Build: %s\n", GitShortSHA1);
	fprintf(theReleaseCrashLogFile, "Process: %lu  Thread: %lu\n",
		static_cast<unsigned long>(::GetCurrentProcessId()),
		static_cast<unsigned long>(::GetCurrentThreadId()));
	fprintf(theReleaseCrashLogFile, "Executable: %s\n", executable);
	fprintf(theReleaseCrashLogFile, "\nReason:\n%s\n", reason ? reason : "<no reason supplied>");

	if ((sourceFile && sourceFile[0]) || (functionName && functionName[0]))
	{
		fprintf(theReleaseCrashLogFile, "\nSource / fault location:\n");
		if (sourceFile && sourceFile[0])
		{
			fprintf(theReleaseCrashLogFile, "%s", sourceFile);
			if (sourceLine > 0)
				fprintf(theReleaseCrashLogFile, ":%d", sourceLine);
		}
		else
		{
			fprintf(theReleaseCrashLogFile, "<source file unavailable>");
		}
		if (functionName && functionName[0])
			fprintf(theReleaseCrashLogFile, " (%s)", functionName);
		fprintf(theReleaseCrashLogFile, "\n");
	}

	fprintf(theReleaseCrashLogFile, "\nLast exception/error context:\n%s\n",
		g_LastErrorDump.isNotEmpty() ? g_LastErrorDump.str() : "<none captured>");

	fprintf(theReleaseCrashLogFile, "\nCurrent stack:\n");
	const int STACKTRACE_SIZE = 24;
	const int STACKTRACE_SKIP = 5;
	void* stacktrace[STACKTRACE_SIZE];
	::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
	::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, releaseCrashLogOutput);

	fflush(theReleaseCrashLogFile);
	fclose(theReleaseCrashLogFile);
	theReleaseCrashLogFile = nullptr;
}

// ----------------------------------------------------------------------------
static void showReleaseCrashDialog(
	const char *reason,
	const char *sourceFile,
	Int sourceLine,
	const char *functionName,
	const char *reportPath)
{
	if (TheGlobalData && TheGlobalData->m_headless)
		return;

	char source[2048];
	if (sourceFile && sourceFile[0])
	{
		if (sourceLine > 0 && functionName && functionName[0])
			snprintf(source, ARRAY_SIZE(source), "%s:%d (%s)", sourceFile, sourceLine, functionName);
		else if (sourceLine > 0)
			snprintf(source, ARRAY_SIZE(source), "%s:%d", sourceFile, sourceLine);
		else
			snprintf(source, ARRAY_SIZE(source), "%s", sourceFile);
	}
	else if (functionName && functionName[0])
	{
		snprintf(source, ARRAY_SIZE(source), "<source file unavailable> (%s)", functionName);
	}
	else
	{
		strlcpy(source, "<not available>", ARRAY_SIZE(source));
	}

	char message[8192];
	snprintf(message, ARRAY_SIZE(message),
		"ZHOverhaul hit a fatal error.\n\n"
		"Reason:\n%s\n\n"
		"Source:\n%s\n\n"
		"Build: %s\n\n"
		"Diagnostic report:\n%s\n\n"
#ifdef RTS_ENABLE_CRASHDUMP
		"Minimal/full crash dumps (Crash*.dmp) are written to the same user-data directory.\n\n"
#endif
		"Please use the report and dumps instead of the old generic 'serious error' message.",
		reason ? reason : "<no reason supplied>",
		source,
		GitShortSHA1,
		reportPath ? reportPath : "<report path unavailable>");

	::MessageBox(nullptr, message, "ZHOverhaul Fatal Error", MB_OK | MB_SYSTEMMODAL | MB_ICONERROR);
}

// ----------------------------------------------------------------------------
void ReleaseCrashWithLocation(const char *reason, const char *sourceFile, int sourceLine, const char *functionName)
{
	if (!DX8Wrapper_IsWindowed && ApplicationHWnd)
		ShowWindow(ApplicationHWnd, SW_HIDE);

	TriggerMiniDump();

	char prevbuf[_MAX_PATH];
	char curbuf[_MAX_PATH];
	buildReleaseCrashPaths(prevbuf, ARRAY_SIZE(prevbuf), curbuf, ARRAY_SIZE(curbuf));
	writeReleaseCrashReport(reason, sourceFile, sourceLine, functionName, prevbuf, curbuf);
	showReleaseCrashDialog(reason, sourceFile, sourceLine, functionName, curbuf);

	_exit(1);
}

// ----------------------------------------------------------------------------
void ReleaseCrash(const char *reason)
{
	ReleaseCrashWithLocation(reason, nullptr, 0, nullptr);
}

static void buildLocalizedCrashReason(const AsciiString& promptKey, const AsciiString& messageKey, AsciiString& outReason)
{
	if (!TheGameText)
	{
		outReason.format("Localized fatal error. Prompt key: %s; Message key: %s",
			promptKey.str(), messageKey.str());
		return;
	}

	UnicodeString prompt = TheGameText->fetch(promptKey);
	UnicodeString message = TheGameText->fetch(messageKey);
	AsciiString promptAscii;
	AsciiString messageAscii;
	promptAscii.translate(prompt);
	messageAscii.translate(message);

	outReason.format(
		"Localized fatal error.\nPrompt: %s\nMessage: %s\nPrompt key: %s\nMessage key: %s",
		promptAscii.str(), messageAscii.str(), promptKey.str(), messageKey.str());
}

void ReleaseCrashLocalizedWithLocation(
	const AsciiString& p,
	const AsciiString& m,
	const char* sourceFile,
	int sourceLine,
	const char* functionName)
{
	AsciiString reason;
	buildLocalizedCrashReason(p, m, reason);
	ReleaseCrashWithLocation(reason.str(), sourceFile, sourceLine, functionName);
}

void ReleaseCrashLocalized(const AsciiString& p, const AsciiString& m)
{
	ReleaseCrashLocalizedWithLocation(p, m, nullptr, 0, nullptr);
}
