/*
	This file is part of libswirl
*/
#include "license/bsd"

#include "win_common.h"

#include <shlobj.h>		// SHGetKnownFolderPath
#include <knownfolders.h>
#include <io.h>			// _get_osfhandle

// Gamepads
u16 kcode[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
u32 vks[4];
s8 joyx[4], joyy[4];
u8 rt[4], lt[4];
// Mouse
extern s32 mo_x_abs;
extern s32 mo_y_abs;
extern u32 mo_buttons;
extern f32 mo_x_delta;
extern f32 mo_y_delta;
extern f32 mo_wheel_delta;
// Keyboard
static Win32KeyboardDevice keyboard(0);

extern int screen_width, screen_height;
bool window_maximized = false;


PCHAR*
	CommandLineToArgvA(
	PCHAR CmdLine,
	int* _argc
	)
{
	PCHAR* argv;
	PCHAR  _argv;
	ULONG   len;
	ULONG   argc;
	CHAR    a;
	ULONG   i, j;

	BOOLEAN  in_QM;
	BOOLEAN  in_TEXT;
	BOOLEAN  in_SPACE;

	len = strlen(CmdLine);
	i = ((len+2)/2)*sizeof(PVOID) + sizeof(PVOID);

	argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
		i + (len+2)*sizeof(CHAR));

	_argv = (PCHAR)(((PUCHAR)argv)+i);

	argc = 0;
	argv[argc] = _argv;
	in_QM = FALSE;
	in_TEXT = FALSE;
	in_SPACE = TRUE;
	i = 0;
	j = 0;

	while( a = CmdLine[i] )
	{
		if(in_QM)
		{
			if(a == '\"')
			{
				in_QM = FALSE;
			}
			else
			{
				_argv[j] = a;
				j++;
			}
		}
		else
		{
			switch(a)
			{
			case '\"':
				in_QM = TRUE;
				in_TEXT = TRUE;
				if(in_SPACE) {
					argv[argc] = _argv+j;
					argc++;
				}
				in_SPACE = FALSE;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				if(in_TEXT)
				{
					_argv[j] = '\0';
					j++;
				}
				in_TEXT = FALSE;
				in_SPACE = TRUE;
				break;
			default:
				in_TEXT = TRUE;
				if(in_SPACE)
				{
					argv[argc] = _argv+j;
					argc++;
				}
				_argv[j] = a;
				j++;
				in_SPACE = FALSE;
				break;
			}
		}
		i++;
	}
	_argv[j] = '\0';
	argv[argc] = NULL;

	(*_argc) = argc;
	return argv;
}


LONG ExeptionHandler(EXCEPTION_POINTERS *ExceptionInfo)
{
	EXCEPTION_POINTERS* ep = ExceptionInfo;

	u32 dwCode = ep->ExceptionRecord->ExceptionCode;

	EXCEPTION_RECORD* pExceptionRecord=ep->ExceptionRecord;

	if (dwCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	unat address=(unat)pExceptionRecord->ExceptionInformation[1];

	rei_host_context_t ctx = { 0 };

	// TODO: Implement context abstraction for windows
	#if HOST_CPU == CPU_X64
		ctx.pc = (unat&)ep->ContextRecord->Rip;
	#else
		#error missing arch for windows
	#endif

	if (virtualDreamcast && virtualDreamcast->HandleFault(address, &ctx))
	{
		// TODO: Implement context abstraction for windows
		#if HOST_CPU == CPU_X64
			(unat&)ep->ContextRecord->Rip = ctx.pc;
		#else
			#error missing arch for windows
		#endif

		return EXCEPTION_CONTINUE_EXECUTION;
	}
	else
	{
		//printf("[GPF] Unhandled access to : 0x%X\n",(unat)address);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

// A path of the system in the code page the rest of the emulator opens files with, the ANSI one; false when the
// path has a character that page does not have
static bool narrow_path(const wchar_t* wide, string& out)
{
	BOOL lossy = FALSE;
	int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, nullptr, 0, nullptr, &lossy);
	if (n <= 0 || lossy)
		return false;
	vector<char> text(n);
	if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, text.data(), n, nullptr, &lossy) != n || lossy)
		return false;
	out = text.data();
	return true;
}

// The directories a run can keep its files in, cfg/cl.cpp chooses: the one of the executable, and %APPDATA%\nulldc3
// for --shared
void SetupPath()
{
	HostDirs dirs;

	// the file of the executable, from the system: never the working directory
	vector<wchar_t> path(MAX_PATH);
	for (;;)
	{
		DWORD n = GetModuleFileNameW(NULL, path.data(), (DWORD)path.size());
		if (n == 0)
		{
			dirs.exe_dir_error = "GetModuleFileNameW failed with error " + std::to_string(GetLastError());
			break;
		}
		if (n < path.size())
		{
			wstring exe(path.data(), n);
			size_t separator = exe.find_last_of(L"\\/");
			wstring dir = separator == wstring::npos ? L"" : exe.substr(0, separator);
			if (!dir.empty() && dir.back() == L':')
				dir += L'\\';	// C:\nulldc3.exe is in C:\, not in the working directory of drive C
			if (dir.empty())
				dirs.exe_dir_error = "the path of the executable has no directory";
			else if (!narrow_path(dir.c_str(), dirs.exe_dir))
				dirs.exe_dir_error = "its path has a character that the ANSI code page of the system does not have";
			break;
		}
		path.resize(path.size() * 2);
	}

	PWSTR appdata = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appdata)))
	{
		string dir;
		if (narrow_path(appdata, dir))
			dirs.shared_config = dirs.shared_data = dir + "\\nulldc3";
		else
			dirs.shared_error = "%APPDATA% has a character that the ANSI code page of the system does not have";
	}
	else
		dirs.shared_error = "the system names no %APPDATA% folder";
	CoTaskMemFree(appdata);

	cl_SetHostDirs(dirs);
}


void ToggleFullscreen()
{
	extern void* window_win;
	static RECT rSaved;
	static bool fullscreen=false;
	HWND hWnd = (HWND)window_win;

	fullscreen = !fullscreen;


	if (fullscreen)
	{
		GetWindowRect(hWnd, &rSaved);

		MONITORINFO mi = { sizeof(mi) };
		HMONITOR hmon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		if (GetMonitorInfo(hmon, &mi)) {

			SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
			SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

			SetWindowPos(hWnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, 
				SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);
		}
	}
	else {
		
		SetWindowLongPtr(hWnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
		SetWindowLongPtr(hWnd, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW | (window_maximized ? WS_MAXIMIZE : 0));

		SetWindowPos(hWnd, NULL, rSaved.left, rSaved.top,
			rSaved.right - rSaved.left, rSaved.bottom - rSaved.top, 
			SWP_SHOWWINDOW|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS|SWP_NOZORDER);
	}
}


BOOL CtrlHandler( DWORD fdwCtrlType )
{
	switch( fdwCtrlType )
	{
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_LOGOFF_EVENT:
		// Pass other signals to the next handler.
		case CTRL_BREAK_EVENT:
		// CTRL-CLOSE: confirm that the user wants to exit.
		case CTRL_CLOSE_EVENT:
		// Handle the CTRL-C signal.
		case CTRL_C_EVENT:
			SendMessageA((HWND)libPvr_GetRenderTarget(),WM_CLOSE,0,0); //FIXEM
			return( TRUE );
		default:
			return FALSE;
	}
}


// Whether what the emulator wrote to stream can still be read when the process has ended: in a file or a pipe, or
// in the console of the cmd.exe or PowerShell it was started from. Not when there is no console, nor in a console
// that has this process alone (the one Windows opens for a start from Explorer or a shortcut, or the one of
// --console): that one closes with it.
static bool output_is_kept(FILE* stream)
{
	int fd = _fileno(stream);
	if (fd < 0)
		return false;
	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	if (handle == INVALID_HANDLE_VALUE || handle == NULL)
		return false;
	DWORD mode;
	if (!GetConsoleMode(handle, &mode))
		return true;	// a file, a pipe, or a device that is no console
	DWORD processes[2];
	return GetConsoleProcessList(processes, 2) > 1;
}

void ReserveBottomMemory()
{
#ifdef _DEBUG
    static bool s_initialized = false;
    if ( s_initialized )
        return;
    s_initialized = true;

    // Start by reserving large blocks of address space, and then
    // gradually reduce the size in order to capture all of the
    // fragments. Technically we should continue down to 64 KB but
    // stopping at 1 MB is sufficient to keep most allocators out.

    const size_t LOW_MEM_LINE = 0x100000000LL;
    size_t totalReservation = 0;
    size_t numVAllocs = 0;
    size_t numHeapAllocs = 0;
    size_t oneMB = 1024 * 1024;
    for (size_t size = 256 * oneMB; size >= oneMB; size /= 2)
    {
        for (;;)
        {
            void* p = VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
            if (!p)
                break;

            if ((size_t)p >= LOW_MEM_LINE)
            {
                // We don't need this memory, so release it completely.
                VirtualFree(p, 0, MEM_RELEASE);
                break;
            }

            totalReservation += size;
            ++numVAllocs;
        }
    }

    // Now repeat the same process but making heap allocations, to use up
    // the already reserved heap blocks that are below the 4 GB line.
    HANDLE heap = GetProcessHeap();
    for (size_t blockSize = 64 * 1024; blockSize >= 16; blockSize /= 2)
    {
        for (;;)
        {
            void* p = HeapAlloc(heap, 0, blockSize);
            if (!p)
                break;

            if ((size_t)p >= LOW_MEM_LINE)
            {
                // We don't need this memory, so release it completely.
                HeapFree(heap, 0, p);
                break;
            }

            totalReservation += blockSize;
            ++numHeapAllocs;
        }
    }

    // Perversely enough the CRT doesn't use the process heap. Suck up
    // the memory the CRT heap has already reserved.
    for (size_t blockSize = 64 * 1024; blockSize >= 16; blockSize /= 2)
    {
        for (;;)
        {
            void* p = malloc(blockSize);
            if (!p)
                break;

            if ((size_t)p >= LOW_MEM_LINE)
            {
                // We don't need this memory, so release it completely.
                free(p);
                break;
            }

            totalReservation += blockSize;
            ++numHeapAllocs;
        }
    }

    // Print diagnostics showing how many allocations we had to make in
    // order to reserve all of low memory, typically less than 200.
    char buffer[1000];
    sprintf_s(buffer, "Reserved %1.3f MB (%d vallocs,"
                      "%d heap allocs) of low-memory.\n",
            totalReservation / (1024 * 1024.0),
            (int)numVAllocs, (int)numHeapAllocs);
    OutputDebugStringA(buffer);
#endif
}

#include "hw/sh4/dyna/ngen.h"

typedef union _UNWIND_CODE {
	struct {
		u8 CodeOffset;
		u8 UnwindOp : 4;
		u8 OpInfo : 4;
	};
	USHORT FrameOffset;
} UNWIND_CODE, *PUNWIND_CODE;

typedef struct _UNWIND_INFO {
	u8 Version : 3;
	u8 Flags : 5;
	u8 SizeOfProlog;
	u8 CountOfCodes;
	u8 FrameRegister : 4;
	u8 FrameOffset : 4;
	//ULONG ExceptionHandler;
	UNWIND_CODE UnwindCode[1];
	/*  UNWIND_CODE MoreUnwindCode[((CountOfCodes + 1) & ~1) - 1];
	*   union {
	*       OPTIONAL ULONG ExceptionHandler;
	*       OPTIONAL ULONG FunctionEntry;
	*   };
	*   OPTIONAL ULONG ExceptionData[]; */
} UNWIND_INFO, *PUNWIND_INFO;

static RUNTIME_FUNCTION Table[1];
static _UNWIND_INFO unwind_info[1];

EXCEPTION_DISPOSITION
__gnat_SEH_error_handler(struct _EXCEPTION_RECORD* ExceptionRecord,
void *EstablisherFrame,
struct _CONTEXT* ContextRecord,
	void *DispatcherContext)
{
	EXCEPTION_POINTERS ep;
	ep.ContextRecord = ContextRecord;
	ep.ExceptionRecord = ExceptionRecord;

	return (EXCEPTION_DISPOSITION)ExeptionHandler(&ep);
}

PRUNTIME_FUNCTION
seh_callback(
_In_ DWORD64 ControlPc,
_In_opt_ PVOID Context
) {
	unwind_info[0].Version = 1;
	unwind_info[0].Flags = UNW_FLAG_UHANDLER;
	/* We don't use the unwinding info so fill the structure with 0 values.  */
	unwind_info[0].SizeOfProlog = 0;
	unwind_info[0].CountOfCodes = 0;
	unwind_info[0].FrameOffset = 0;
	unwind_info[0].FrameRegister = 0;
	/* Add the exception handler.  */

//		unwind_info[0].ExceptionHandler =
	//	(DWORD)((u8 *)__gnat_SEH_error_handler - CodeCache);
	/* Set its scope to the entire program.  */
	Table[0].BeginAddress = 0;// (CodeCache - (u8*)__ImageBase);
	Table[0].EndAddress = /*(CodeCache - (u8*)__ImageBase) +*/ CODE_SIZE;
	Table[0].UnwindData = (DWORD)((u8 *)unwind_info - CodeCache);
	printf("TABLE CALLBACK\n");
	//for (;;);
	return Table;
}
void setup_seh() {
#if 1
	/* Get the base of the module.  */
	//u8* __ImageBase = (u8*)GetModuleHandle(NULL);
	/* Current version is always 1 and we are registering an
	exception handler.  */
	unwind_info[0].Version = 1;
	unwind_info[0].Flags = UNW_FLAG_NHANDLER;
	/* We don't use the unwinding info so fill the structure with 0 values.  */
	unwind_info[0].SizeOfProlog = 0;
	unwind_info[0].CountOfCodes = 1;
	unwind_info[0].FrameOffset = 0;
	unwind_info[0].FrameRegister = 0;
	/* Add the exception handler.  */

	unwind_info[0].UnwindCode[0].CodeOffset = 0;
	unwind_info[0].UnwindCode[0].UnwindOp = 2;// UWOP_ALLOC_SMALL;
	unwind_info[0].UnwindCode[0].OpInfo = 0x20 / 8;

	//unwind_info[0].ExceptionHandler =
		//(DWORD)((u8 *)__gnat_SEH_error_handler - CodeCache);
	/* Set its scope to the entire program.  */
	Table[0].BeginAddress = 0;// (CodeCache - (u8*)__ImageBase);
	Table[0].EndAddress = /*(CodeCache - (u8*)__ImageBase) +*/ CODE_SIZE;
	Table[0].UnwindData = (DWORD)((u8 *)unwind_info - CodeCache);
	/* Register the unwind information.  */
	RtlAddFunctionTable(Table, 1, (DWORD64)CodeCache);
#endif

	//verify(RtlInstallFunctionTableCallback((unat)CodeCache | 0x3, (DWORD64)CodeCache, CODE_SIZE, seh_callback, 0, 0));
}





// DEF_CONSOLE allows you to override linker subsystem and therefore default console //
//	: pragma isn't pretty but def's are configurable 
#ifdef DEF_CONSOLE
#pragma comment(linker, "/subsystem:console")

int main(int argc, char **argv)
{
#else
#pragma comment(linker, "/subsystem:windows")

int CALLBACK WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShowCmd)

{
	int argc=0;
	wchar* cmd_line=GetCommandLineA();
	wchar** argv=CommandLineToArgvA(cmd_line,&argc);
	// --console is looked for here, ahead of the command line (cfg/cl.cpp, which takes it and --log), so that the
	// console shows what that says as well; a "--" ends the switches here too
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--"))
			break;
		if (!strcmp(argv[i], "--console"))
		{
			if (AllocConsole())
			{
				freopen("CON", "w", stdout);
				freopen("CON", "w", stderr);
				freopen("CON", "r", stdin);
			}
			SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
			break;
		}
	}

#endif

	ReserveBottomMemory();
	SetupPath();

	AddVectoredExceptionHandler(1, ExeptionHandler);
#ifndef __GNUC__
	__try
#endif
	{
		CoInitialize(nullptr);

		int init_rv = reicast_init(argc, argv);
		if (init_rv == rv_cli_finish || init_rv == rv_cli_error)
		{
			// --help (the usage text is on stdout, and is all that was asked for) or a command line that was refused
			// (the reason is on stderr): where that cannot be read once the process has ended, without a console
			// or in one that closes with the process, a message box shows it as well
			bool help = init_rv == rv_cli_finish;
			if (!output_is_kept(help ? stdout : stderr))
				MessageBoxA(NULL, (help ? cl_HelpText() : cl_ErrorText()).c_str(), "nullDC",
				            MB_OK | (help ? MB_ICONINFORMATION : MB_ICONERROR));
			return help ? 0 : 2;
		}
		if (init_rv != 0)
			die("nullDC initialization failed");

		setup_seh();

        reicast_ui_loop();

        reicast_term();
	}
#ifndef __GNUC__
	__except( ExeptionHandler(GetExceptionInformation()) )
	{
		printf("Unhandled exception - Emulation thread halted...\n");
	}
#endif
	SetUnhandledExceptionFilter(0);
	cfgSaveBool("windows", "maximized", window_maximized);
	if (!window_maximized && screen_width != 0 && screen_height != 0)
	{
		cfgSaveInt("windows", "width", screen_width);
		cfgSaveInt("windows", "height", screen_height);
	}

	return 0;
}

int get_mic_data(u8* buffer) { return 0; }
int push_vmu_screen(u8* buffer) { return 0; }
