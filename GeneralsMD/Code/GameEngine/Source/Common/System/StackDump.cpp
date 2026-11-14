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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#if defined(_DEBUG) || defined(_INTERNAL) || defined(IG_DEBUG_STACKTRACE)

#pragma pack(push, 8)

#pragma comment(linker, "/defaultlib:Dbghelp.lib")

#include "Common/StackDump.h"
#include "Common/Debug.h"
#include "Common/FileWriteQueue.h"


//*****************************************************************************
//	Prototypes
//*****************************************************************************
BOOL InitSymbolInfo(void);
void UninitSymbolInfo(void);
void MakeStackTrace(DWORD_PTR myeip,DWORD_PTR myesp,DWORD_PTR myebp, int skipFrames, void (*callback)(const char*));
void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address);
void WriteStackLine(void*address, void (*callback)(const char*));

//*****************************************************************************
//	Mis-named globals :-)
//*****************************************************************************
static CONTEXT gsContext;
static Bool gsInit=FALSE;

BOOL (__stdcall *gsSymGetLineFromAddr)(
		IN  HANDLE                  hProcess,
		IN  DWORD64                 dwAddr,
		OUT PDWORD                  pdwDisplacement,
		OUT PIMAGEHLP_LINE64        Line
			);


//*****************************************************************************
//*****************************************************************************
void StackDumpDefaultHandler(const char*line)
{
	DEBUG_LOG((line));
}


//*****************************************************************************
//*****************************************************************************
void StackDump(void (*callback)(const char*))
{
	if (callback == NULL) 
	{
		callback = StackDumpDefaultHandler;
	}

	InitSymbolInfo();

	DWORD_PTR myeip,myesp,myebp;

	// The register set to start the walk from.
	CONTEXT here;
	memset(&here, 0, sizeof(here));
	here.ContextFlags = CONTEXT_FULL;
	RtlCaptureContext(&here);
#if defined(_M_ARM64)
	myeip = here.Pc;
	myesp = here.Sp;
	myebp = here.Fp;
#else
	myeip = here.Rip;
	myesp = here.Rsp;
	myebp = here.Rbp;
#endif

	MakeStackTrace(myeip,myesp,myebp, 2, callback);
}


//*****************************************************************************
//*****************************************************************************
void StackDumpFromContext(DWORD_PTR eip,DWORD_PTR esp,DWORD_PTR ebp, void (*callback)(const char*))
{
	if (callback == NULL) 
	{
		callback = StackDumpDefaultHandler;
	}

	InitSymbolInfo();

	MakeStackTrace(eip,esp,ebp, 0,  callback);
}


//*****************************************************************************
//*****************************************************************************
BOOL InitSymbolInfo()
{
	if (gsInit == TRUE) 
		return TRUE;

	gsInit = TRUE;

	atexit(UninitSymbolInfo);

	// See if we have the line from address function
	// We use GetProcAddress to stop link failures at dll loadup
	HINSTANCE hInstDebugHlp = GetModuleHandle("dbghelp.dll");

	gsSymGetLineFromAddr = (BOOL (__stdcall *)(	IN  HANDLE,IN  DWORD64,OUT PDWORD,OUT PIMAGEHLP_LINE64))
							GetProcAddress(hInstDebugHlp , "SymGetLineFromAddr64");

	::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_OMAP_FIND_NEAREST);

	HANDLE process = GetCurrentProcess();

	// The old code built its own search path out of _splitpath, but _splitpath
	// already hands back "D:" in drive and the format string added a second colon,
	// so the path ("D::\dir\") pointed nowhere and every frame came out
	// "<Unknown>".  fInvadeProcess enumerates the process' own modules instead,
	// which finds the PDB sitting next to the exe without a path at all.
	return ::SymInitialize(process, NULL, TRUE);
}


//*****************************************************************************
//*****************************************************************************
void UninitSymbolInfo(void)
{
	if (gsInit == FALSE)
	{
		return;
	}

	gsInit = FALSE;

	::SymCleanup(GetCurrentProcess());
}



//*****************************************************************************
//*****************************************************************************
void MakeStackTrace(DWORD_PTR myeip,DWORD_PTR myesp,DWORD_PTR myebp, int skipFrames, void (*callback)(const char*))
{
// The unwind comes from the exception tables rather than a frame pointer chain, and StackWalk64
// reads and writes the whole register set to do it: a NULL context walks nowhere.  The three
// addresses are all a caller hands over, so the rest of the context is captured here and
// overwritten with them.
#if defined(_M_ARM64)
const DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
#else
const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#endif
CONTEXT walkContext;
memset(&walkContext, 0, sizeof(walkContext));
walkContext.ContextFlags = CONTEXT_FULL;
RtlCaptureContext(&walkContext);
#if defined(_M_ARM64)
walkContext.Pc = myeip;
walkContext.Sp = myesp;
walkContext.Fp = myebp;
#else
walkContext.Rip = myeip;
walkContext.Rsp = myesp;
walkContext.Rbp = myebp;
#endif
STACKFRAME64    stack_frame;
BOOL            b_ret = TRUE;

HANDLE thread = GetCurrentThread();
HANDLE process = GetCurrentProcess();

memset(&gsContext, 0, sizeof(CONTEXT));
gsContext.ContextFlags = CONTEXT_FULL;

memset(&stack_frame, 0, sizeof(stack_frame));
stack_frame.AddrPC.Mode = AddrModeFlat;
stack_frame.AddrPC.Offset = myeip;
stack_frame.AddrStack.Mode = AddrModeFlat;
stack_frame.AddrStack.Offset = myesp;
stack_frame.AddrFrame.Mode = AddrModeFlat;
stack_frame.AddrFrame.Offset = myebp;
{
/*
    if(GetThreadContext(thread, &gsContext))
    {
        memset(&stack_frame, 0, sizeof(stack_frame));
        stack_frame.AddrPC.Mode = AddrModeFlat;
        stack_frame.AddrPC.Offset = gsContext.Eip;
        stack_frame.AddrStack.Mode = AddrModeFlat;
        stack_frame.AddrStack.Offset = gsContext.Esp;
        stack_frame.AddrFrame.Mode = AddrModeFlat;
        stack_frame.AddrFrame.Offset = gsContext.Ebp;
*/

		//{
			callback("Call Stack\n**********\n");

			// Skip some ?
			unsigned int skip = skipFrames;
			while (b_ret&&skip)
			{
					b_ret = StackWalk64(    machineType,
											process,
											thread,
											&stack_frame,
											&walkContext,
											NULL,
											SymFunctionTableAccess64,
											SymGetModuleBase64,
											NULL);
					skip--;
			}

			skip = 30;
			while(b_ret&&skip)
			{

					b_ret = StackWalk64(    machineType,
											process,
											thread,
											&stack_frame,
											&walkContext,
											NULL,
											SymFunctionTableAccess64,
											SymGetModuleBase64,
											NULL);



					if (b_ret) WriteStackLine((void *) stack_frame.AddrPC.Offset, callback);
					skip--;
			}
	}
}


//*****************************************************************************
//*****************************************************************************
// The symbol name can be up to 512 characters and the source path up to MAX_PATH, and both used to
// be written with unbounded calls into buffers of exactly those sizes - a long symbol plus "();"
// walked off the end while the process was already reporting a crash.
void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address)
{
	InitSymbolInfo();
	if (name)
	{
		strlcpy(name, "<Unknown>", nameSize);
	}
	if (filename)
	{
		strlcpy(filename, "<Unknown>", filenameSize);
	}
	if (linenumber)
	{
		*linenumber = 0xFFFFFFFF;
	}
	if (address)
	{
		*address = 0xFFFFFFFF;
	}

	DWORD64 displacement = 0;

    HANDLE process = ::GetCurrentProcess();

    char symbol_buffer[512 + sizeof(IMAGEHLP_SYMBOL64)];
    memset(symbol_buffer, 0, sizeof(symbol_buffer));

    PIMAGEHLP_SYMBOL64 psymbol = (PIMAGEHLP_SYMBOL64)symbol_buffer;
    psymbol->SizeOfStruct = sizeof(symbol_buffer);
    psymbol->MaxNameLength = 512;

    if (SymGetSymFromAddr64(process, (DWORD64)(DWORD_PTR) pointer, &displacement, psymbol))
    {
		if (name)
		{
			strlcpy(name, psymbol->Name, nameSize);
			strlcat(name, "();", nameSize);
		}

		// Get line now
		if (gsSymGetLineFromAddr)
		{
			// Unsupported for win95/98 at least with my current dbghelp.dll

			IMAGEHLP_LINE64 line;
			memset(&line,0,sizeof(line));
			line.SizeOfStruct = sizeof(line);

			DWORD lineDisplacement = 0;
			if (gsSymGetLineFromAddr(process, (DWORD64)(DWORD_PTR) pointer, &lineDisplacement, &line))
			{
				if (filename)
				{
					strlcpy(filename, line.FileName, filenameSize);
				}
				if (linenumber)
				{
					*linenumber = (unsigned int)line.LineNumber;
				}
				if (address)
				{
					*address = (unsigned int)line.Address;
				}
			}
		}
    }
	else if (name)
	{
		/* No symbol for it, which for anything outside this exe is the normal case: a system DLL,
			 the Direct3D runtime, a display driver.  The stack then read `<Unknown> 0x582843F0` for
			 every one of those frames, which says nothing at all - a crash inside a driver during a
			 device reset looked exactly like a crash inside the game.  The module and the offset into
			 it are free and are the whole of the answer: `d3d9.dll+0x143f0` is something that can
			 be looked up, argued about, and worked around. */
		HMODULE module = NULL;
		if (::GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
														 (LPCSTR)pointer, &module ) && module != NULL)
		{
			char modulePath[ MAX_PATH ];
			modulePath[ 0 ] = 0;
			if (::GetModuleFileName( module, modulePath, sizeof( modulePath ) ) > 0)
			{
				const char *leaf = strrchr( modulePath, '\\' );
				leaf = leaf ? leaf + 1 : modulePath;
				snprintf( name, nameSize, "%s+0x%X;", leaf, (unsigned int)((char *)pointer - (char *)module) );
			}
		}
	}
}


//*****************************************************************************
// Gets last x addresses from the stack
//*****************************************************************************
void FillStackAddresses(void**addresses, unsigned int count, unsigned int skip)
{
	InitSymbolInfo();

	// This used to seed a STACKFRAME from inline asm and drive StackWalk() by
	// hand.  On this toolchain that walk returns the seeded frame once and then
	// stops, so every crash report came back with an empty stack.  CaptureStack-
	// BackTrace does the same job in one call and needs no symbols to do it; the
	// +1 drops this function's own frame, which the old code also skipped.
	memset(addresses, 0, count * sizeof(void *));
	const USHORT captured = ::CaptureStackBackTrace(skip + 1, count, addresses, NULL);

	// Reading the count back keeps the call from being the last thing this function does: as a tail
	// call it drops this frame, and the +1 above then skips the caller's frame instead of this one,
	// which is how a stack dump came back starting one level too high.
	if (captured == 0 && count > 0)
	{
		addresses[0] = NULL;
	}
}



//*****************************************************************************
// Do full stack dump using an address array
//*****************************************************************************
void StackDumpFromAddresses(void**addresses, unsigned int count, void (*callback)(const char *))
{
	if (callback == NULL) 
	{
		callback = StackDumpDefaultHandler;
	}

	InitSymbolInfo();

	while ((count--) && (*addresses!=NULL))
	{
		WriteStackLine(*addresses,callback);	
		addresses++;
	}	
}


AsciiString g_LastErrorDump;
//*****************************************************************************
//*****************************************************************************
void WriteStackLine(void*address, void (*callback)(const char*))
{
	// Big enough for the longest thing that can be formatted into it: a path, a symbol, and the
	// punctuation between them.  It used to be MAX_PATH and could not hold its own contents.
	static char line[MAX_PATH + 512 + 64];
	static char function_name[512];
	static char filename[MAX_PATH];
	unsigned int linenumber;
	unsigned int addr;

	GetFunctionDetails(address, function_name, ARRAY_SIZE(function_name), filename, ARRAY_SIZE(filename), &linenumber, &addr);
	// A path of MAX_PATH plus a symbol of 512 does not fit in MAX_PATH, so this line always could
	// overrun; it just needed a deep enough source tree and a long enough C++ name to do it.
    snprintf(line, ARRAY_SIZE(line), "  %s(%d) : %s 0x%08p", filename, linenumber, function_name, address);
		if (g_LastErrorDump.isNotEmpty()) {
			g_LastErrorDump.concat(line);
			g_LastErrorDump.concat("\n");
		}
	callback(line);
	callback("\n");
}


static HANDLE gsCrashRecordHandle = NULL;	// the debug log's own handle, set when it opens

void SetCrashRecordHandle( void *handle )
{
	gsCrashRecordHandle = (HANDLE)handle;
}

static char *appendText( char *out, const char *text )
{
	while (*text)
		*out++ = *text++;
	return out;
}

static char *appendHex( char *out, unsigned long long value )
{
	out = appendText( out, "0x" );
	for (int shift = 60; shift >= 0; shift -= 4)
		*out++ = "0123456789ABCDEF"[(value >> shift) & 15];
	return out;
}

/* Written before anything that allocates, locks or formats.  A second fault inside the dump below, or
	 a lock the faulting thread already held, left a log with no trace of the crash at all, its last
	 queued second included (a 0xC0000005 report whose tail was an ordinary frame).  A stack buffer, no
	 CRT, and WriteFile straight into the log file past its queue; the queued lines follow it. */
static void writeCrashRecord( EXCEPTION_POINTERS *e_info )
{
	if (gsCrashRecordHandle == NULL)
		return;		// no log file open
	void *pc = e_info->ExceptionRecord->ExceptionAddress;
	void *base = NULL;
	RtlPcToFileHeader( pc, &base );

	char record[ 256 ];
	char *out = appendText( record, "\r\nEXCEPTION RECORD code " );
	out = appendHex( out, e_info->ExceptionRecord->ExceptionCode );
	out = appendText( out, " at " );
	out = appendHex( out, (ULONG_PTR)pc );
	out = appendText( out, " module " );
	out = appendHex( out, (ULONG_PTR)base );
	out = appendText( out, " + " );
	out = appendHex( out, (ULONG_PTR)pc - (ULONG_PTR)base );
	out = appendText( out, " thread " );
	out = appendHex( out, GetCurrentThreadId() );
	out = appendText( out, "\r\n" );
	// At the end each time: the log writer may be mid-flush, and a write at the handle's own pointer
	// would land on its bytes.
	DWORD written;
	SetFilePointer( gsCrashRecordHandle, 0, NULL, FILE_END );
	WriteFile( gsCrashRecordHandle, record, (DWORD)(out - record), &written, NULL );

	// The module's name second: GetModuleFileName takes the loader lock, which the faulting thread may hold.
	char name[ MAX_PATH + 2 ];
	const DWORD length = base != NULL ? GetModuleFileNameA( (HMODULE)base, name, MAX_PATH ) : 0;
	if (length != 0)
	{
		name[ length ] = '\r';
		name[ length + 1 ] = '\n';
		SetFilePointer( gsCrashRecordHandle, 0, NULL, FILE_END );
		WriteFile( gsCrashRecordHandle, name, length + 2, &written, NULL );
	}
}

//*****************************************************************************
//*****************************************************************************
void DumpExceptionInfo( unsigned int u, EXCEPTION_POINTERS* e_info )
{
	writeCrashRecord( e_info );
	FileWriteQueue::flushAll();		// the last second of queued lines, before the dump and its stack walk, which can fault
   DEBUG_LOG(( "\n********** EXCEPTION DUMP ****************\n" ));
	/*
	** List of possible exceptions
	*/
	 g_LastErrorDump.clear(); 

	static const unsigned int _codes[] = {
		EXCEPTION_ACCESS_VIOLATION,
		EXCEPTION_ARRAY_BOUNDS_EXCEEDED,
		EXCEPTION_BREAKPOINT,
		EXCEPTION_DATATYPE_MISALIGNMENT,
		EXCEPTION_FLT_DENORMAL_OPERAND,
		EXCEPTION_FLT_DIVIDE_BY_ZERO,
		EXCEPTION_FLT_INEXACT_RESULT,
		EXCEPTION_FLT_INVALID_OPERATION,
		EXCEPTION_FLT_OVERFLOW,
		EXCEPTION_FLT_STACK_CHECK,
		EXCEPTION_FLT_UNDERFLOW,
		EXCEPTION_ILLEGAL_INSTRUCTION,
		EXCEPTION_IN_PAGE_ERROR,
		EXCEPTION_INT_DIVIDE_BY_ZERO,
		EXCEPTION_INT_OVERFLOW,
		EXCEPTION_INVALID_DISPOSITION,
		EXCEPTION_NONCONTINUABLE_EXCEPTION,
		EXCEPTION_PRIV_INSTRUCTION,
		EXCEPTION_SINGLE_STEP,
		EXCEPTION_STACK_OVERFLOW,
		0xffffffff
	};

	/*
	** Information about each exception type.
	*/
	static char const * _code_txt[] = {
		"Error code: EXCEPTION_ACCESS_VIOLATION\nDescription: The thread tried to read from or write to a virtual address for which it does not have the appropriate access.",
		"Error code: EXCEPTION_ARRAY_BOUNDS_EXCEEDED\nDescription: The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.",
		"Error code: EXCEPTION_BREAKPOINT\nDescription: A breakpoint was encountered.",
		"Error code: EXCEPTION_DATATYPE_MISALIGNMENT\nDescription: The thread tried to read or write data that is misaligned on hardware that does not provide alignment. For example, 16-bit values must be aligned on 2-byte boundaries; 32-bit values on 4-byte boundaries, and so on.",
		"Error code: EXCEPTION_FLT_DENORMAL_OPERAND\nDescription: One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.",
		"Error code: EXCEPTION_FLT_DIVIDE_BY_ZERO\nDescription: The thread tried to divide a floating-point value by a floating-point divisor of zero.",
		"Error code: EXCEPTION_FLT_INEXACT_RESULT\nDescription: The result of a floating-point operation cannot be represented exactly as a decimal fraction.",
		"Error code: EXCEPTION_FLT_INVALID_OPERATION\nDescription: Some strange unknown floating point operation was attempted.",
		"Error code: EXCEPTION_FLT_OVERFLOW\nDescription: The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.",
		"Error code: EXCEPTION_FLT_STACK_CHECK\nDescription: The stack overflowed or underflowed as the result of a floating-point operation.",
		"Error code: EXCEPTION_FLT_UNDERFLOW\nDescription:	The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.",
		"Error code: EXCEPTION_ILLEGAL_INSTRUCTION\nDescription:	The thread tried to execute an invalid instruction.",
		"Error code: EXCEPTION_IN_PAGE_ERROR\nDescription:	The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.",
		"Error code: EXCEPTION_INT_DIVIDE_BY_ZERO\nDescription: The thread tried to divide an integer value by an integer divisor of zero.",
		"Error code: EXCEPTION_INT_OVERFLOW\nDescription: The result of an integer operation caused a carry out of the most significant bit of the result.",
		"Error code: EXCEPTION_INVALID_DISPOSITION\nDescription: An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.",
		"Error code: EXCEPTION_NONCONTINUABLE_EXCEPTION\nDescription: The thread tried to continue execution after a noncontinuable exception occurred.",
		"Error code: EXCEPTION_PRIV_INSTRUCTION\nDescription: The thread tried to execute an instruction whose operation is not allowed in the current machine mode.",
		"Error code: EXCEPTION_SINGLE_STEP\nDescription: A trace trap or other single-instruction mechanism signaled that one instruction has been executed.",
		"Error code: EXCEPTION_STACK_OVERFLOW\nDescription: The thread used up its stack.",
		"Error code: ?????\nDescription: Unknown exception."
	};

	DEBUG_LOG( ("Dump exception info\n") );
	CONTEXT *context = e_info->ContextRecord;
	/*
	** The following are set for access violation only
	*/
	int access_read_write=-1;
	ULONG_PTR access_address = 0;	// unsigned long is 32 bits on Win64 and cut every x64 address in half
	AsciiString msg; 

// DOUBLE_DEBUG does a DEBUG_LOG, and concats to g_LastErrorDump.  jba.
#define DOUBLE_DEBUG(x) { msg.format x; g_LastErrorDump.concat(msg); DEBUG_LOG( x ); }

	if ( e_info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION )
	{
		DOUBLE_DEBUG (("Exception is access violation\n"));
		access_read_write = e_info->ExceptionRecord->ExceptionInformation[0];  // 0=read, 1=write
		access_address = e_info->ExceptionRecord->ExceptionInformation[1];
	}
	else
	{
		DOUBLE_DEBUG (("Exception code is %x\n", e_info->ExceptionRecord->ExceptionCode));
	}
	Int *winMainAddr = (Int *)WinMain;
	DOUBLE_DEBUG(("WinMain at %x\n", winMainAddr));
	/*
	** Match the exception type with the error string and print it out
	*/
	// i indexes _code_txt after the loop, so it outlives the for-scope; the last
	// _code_txt entry is the catch-all that pairs with the 0xffffffff sentinel.
	int i;
	for ( i=0 ; _codes[i] != 0xffffffff ; i++ )
	{
		if ( _codes[i] == e_info->ExceptionRecord->ExceptionCode )
		{
			DEBUG_LOG ( ("Found exception description\n") );
			break;
		}
	}
	DOUBLE_DEBUG( ("%s\n", _code_txt[i]));
	/** For access violations, print out the violation address and if it was read or write.
	*/
	if ( e_info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION )
	{
		if ( access_read_write )
		{
			DOUBLE_DEBUG( ("Access address:%016llX was written to.\n", (unsigned long long)access_address));
		}
		else
		{
			DOUBLE_DEBUG( ("Access address:%016llX was read from.\n", (unsigned long long)access_address));
		}
	}

	/*
	** Registers and the faulting bytes FIRST, before the symbolized stack.
	**
	** StackDumpFromContext walks the frame chain and symbolizes it through dbghelp against an 80MB
	** PDB, which is by far the most fragile thing in this handler - and if it faults, the process
	** goes away with everything below it unwritten.  That has already happened once here: a crash
	** on a worker thread left a log that ended at the words "Stack Dump:" and nothing else, so the
	** faulting address itself was lost and the only evidence left was a module offset in the
	** Windows event log.
	**
	** Eip, Esp, Ebp and the bytes at Eip are enough to find any fault offline against the .map and
	** the PDB.  They cost nothing and they are what a second crash must not be allowed to eat.
	*/
	DOUBLE_DEBUG (("\nDetails:\n"));

	DOUBLE_DEBUG (("Register dump...\n"));

	/*
	** Dump the registers.
	*/
#if defined(_M_ARM64)
	// Windows on Arm: the program counter, stack, frame and link registers, then X0 to X28.
	const DWORD_PTR faultPC = context->Pc;
	DOUBLE_DEBUG ( ( "Pc :%016llX\tSp :%016llX\tFp :%016llX\tLr :%016llX\n", context->Pc, context->Sp, context->Fp, context->Lr));
	for (int x = 0; x < 28; x += 4)
		DOUBLE_DEBUG ( ( "X%-2d:%016llX\tX%-2d:%016llX\tX%-2d:%016llX\tX%-2d:%016llX\n", x, context->X[x], x + 1, context->X[x + 1], x + 2, context->X[x + 2], x + 3, context->X[x + 3]));
	DOUBLE_DEBUG ( ( "X28:%016llX\tCpsr:%08X\n", context->X[28], context->Cpsr));
#else
	const DWORD_PTR faultPC = context->Rip;
	DOUBLE_DEBUG ( ( "Rip:%016llX\tRsp:%016llX\tRbp:%016llX\n", context->Rip, context->Rsp, context->Rbp));
	DOUBLE_DEBUG ( ( "Rax:%016llX\tRbx:%016llX\tRcx:%016llX\n", context->Rax, context->Rbx, context->Rcx));
	DOUBLE_DEBUG ( ( "Rdx:%016llX\tRsi:%016llX\tRdi:%016llX\n", context->Rdx, context->Rsi, context->Rdi));
	DOUBLE_DEBUG ( ( "R8 :%016llX\tR9 :%016llX\tR10:%016llX\n", context->R8, context->R9, context->R10));
	DOUBLE_DEBUG ( ( "R11:%016llX\tR12:%016llX\tR13:%016llX\n", context->R11, context->R12, context->R13));
	DOUBLE_DEBUG ( ( "R14:%016llX\tR15:%016llX\n", context->R14, context->R15));
	DOUBLE_DEBUG ( ( "EFlags:%08X \n", context->EFlags));
	DOUBLE_DEBUG ( ( "CS:%04x  SS:%04x  DS:%04x  ES:%04x  FS:%04x  GS:%04x\n", context->SegCs, context->SegSs, context->SegDs, context->SegEs, context->SegFs, context->SegGs));
#endif

	/*
	** Dump the bytes at EIP. This will make it easier to match the crash address with later versions of the game.
	*/
	char scrap[512];
	DOUBLE_DEBUG ( ("EIP bytes dump...\n"));
	wsprintf (scrap, "\nBytes at CS:EIP (%p)  : ", (void *)faultPC);

	unsigned char *eip_ptr = (unsigned char *) faultPC;
	char bytestr[32];

	for (int c = 0 ; c < 32 ; c++)
	{
		if (IsBadReadPtr(eip_ptr, 1))
		{
			lstrcat (scrap, "?? ");
		}
		else
		{
			snprintf (bytestr, ARRAY_SIZE(bytestr), "%02X ", *eip_ptr);
			strlcat (scrap, bytestr, ARRAY_SIZE(scrap));
		}
		eip_ptr++;
	}

	strlcat (scrap, "\n", ARRAY_SIZE(scrap));
	DOUBLE_DEBUG ( ( (scrap)));

	/*
	** ...and only now the part that can die trying: the symbolized stack.  Everything above is
	** already in the log by this point, so a fault in here costs the stack and nothing else.
	*/
	DOUBLE_DEBUG (("\nStack Dump:\n"));
#if defined(_M_ARM64)
	StackDumpFromContext(context->Pc, context->Sp, context->Fp, NULL);
#else
	StackDumpFromContext(context->Rip, context->Rsp, context->Rbp, NULL);
#endif

  DEBUG_LOG(( "********** END EXCEPTION DUMP ****************\n\n" ));
}																									 


#pragma pack(pop)

#endif

