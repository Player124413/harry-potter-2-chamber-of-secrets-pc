#include "CorePrivate.h"
#include "HP2RngSeed.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <lmcons.h>
#include <objbase.h>
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#include <SDL.h>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace
{
	static TCHAR GBaseDirBuffer[32768] = {0};
	static TCHAR GUserDirBuffer[32768] = {0};
	static TCHAR* GBaseDir = GBaseDirBuffer;
	static TCHAR* GUserDir = GUserDirBuffer;
	static char GHP2StaticLinkHandle;
	static int GWindowsDiagnosticRngSeeded=0;
	static unsigned GWindowsDiagnosticRngRequestedSeed=0;
	static unsigned GWindowsDiagnosticRngEffectiveSeed=0;
	using FClock = std::chrono::steady_clock;
	static const FClock::time_point GStart = FClock::now();
	static std::wstring Wide(const TCHAR* Text) { return std::wstring(reinterpret_cast<const wchar_t*>(Text ? Text : TEXT(""))); }
	static void CopyWide(TCHAR* Out, const wchar_t* Text)
	{
		size_t I=0;
		for (; Text[I] && I<32766; ++I) Out[I]=static_cast<TCHAR>(Text[I]);
		Out[I]=0;
	}
	static double Elapsed() { return std::chrono::duration<double>(FClock::now() - GStart).count(); }
	static void SetRoot(TCHAR* Out, const TCHAR* Root)
	{
		std::error_code EC;
		std::filesystem::path P(reinterpret_cast<const wchar_t*>(Root ? Root : TEXT("")));
		std::filesystem::path Canon=std::filesystem::weakly_canonical(P,EC);
		std::wstring W=(EC?P:Canon).wstring();
		if (!W.empty() && W.back() != L'/' && W.back() != L'\\') W.push_back(L'/');
		CopyWide(Out,W.c_str());
	}
}
ANSICHAR GModule[32] = {0};

static void Recurse()
{
	guard(Recurse);
	Recurse();
	unguard;
}

USystem::USystem()
: SavePath(E_NoInit), CachePath(E_NoInit), CacheExt(E_NoInit), Paths(E_NoInit), Suppress(E_NoInit)
{}

void USystem::StaticConstructor()
{
	guard(USystem::StaticConstructor);
	new(GetClass(),TEXT("PurgeCacheDays"),RF_Public)UIntProperty(CPP_PROPERTY(PurgeCacheDays),TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("SavePath"),RF_Public)UStrProperty(CPP_PROPERTY(SavePath),TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("CachePath"),RF_Public)UStrProperty(CPP_PROPERTY(CachePath),TEXT("Options"),CPF_Config);
	new(GetClass(),TEXT("CacheExt"),RF_Public)UStrProperty(CPP_PROPERTY(CacheExt),TEXT("Options"),CPF_Config);
	UArrayProperty* A=new(GetClass(),TEXT("Paths"),RF_Public)UArrayProperty(CPP_PROPERTY(Paths),TEXT("Options"),CPF_Config);
	A->Inner=new(A,TEXT("StrProperty0"),RF_Public)UStrProperty;
	UArrayProperty* B=new(GetClass(),TEXT("Suppress"),RF_Public)UArrayProperty(CPP_PROPERTY(Suppress),TEXT("Options"),CPF_Config);
	B->Inner=new(B,TEXT("NameProperty0"),RF_Public)UNameProperty;
	unguard;
}

UBOOL USystem::Exec(const TCHAR* Cmd,FOutputDevice& Ar)
{
	if(ParseCommand(&Cmd,TEXT("MEMSTAT")))
	{
		MEMORYSTATUSEX Status={}; Status.dwLength=sizeof(Status);
		GlobalMemoryStatusEx(&Status);
		Ar.Logf(TEXT("Memory available: Phys=%iK Pagef=%iK Virt=%iK"),Status.ullAvailPhys/1024,Status.ullAvailPageFile/1024,Status.ullAvailVirtual/1024);
		Ar.Logf(TEXT("Memory load = %i%%"),Status.dwMemoryLoad);
		return 1;
	}
	if(ParseCommand(&Cmd,TEXT("CONFIGHASH"))) { GConfig->Dump(Ar); return 1; }
	if(ParseCommand(&Cmd,TEXT("RELAUNCH")))
	{
		GConfig->Flush(0);
		TCHAR Executable[32768];
		const DWORD Length=GetModuleFileNameW(nullptr,Executable,ARRAY_COUNT(Executable));
		if(!Length || Length>=ARRAY_COUNT(Executable)) appErrorf(TEXT("Unable to locate executable for relaunch."));
		ShellExecuteW(nullptr,L"open",Executable,Cmd,appBaseDir(),SW_SHOWNORMAL);
		appRequestExit(0);
		return 1;
	}
	if(ParseCommand(&Cmd,TEXT("DEBUG")))
	{
		if(ParseCommand(&Cmd,TEXT("CRASH"))) { appErrorf(TEXT("%s"),TEXT("Unreal crashed at your request")); return 1; }
		if(ParseCommand(&Cmd,TEXT("GPF"))) { Ar.Log(TEXT("Unreal crashing with voluntary GPF")); *(int*)nullptr=123; return 1; }
		if(ParseCommand(&Cmd,TEXT("RECURSE"))) { Ar.Logf(TEXT("Recursing")); Recurse(); return 1; }
		if(ParseCommand(&Cmd,TEXT("EATMEM"))) { for(;;) { void* Eat=appMalloc(65536,TEXT("EatMem")); memset(Eat,0,65536); } }
		return 0;
	}
	return 0;
}
IMPLEMENT_CLASS(USystem);

CORE_API UBOOL appToUtf8NativePath(const TCHAR* Filename, std::string& Out)
{
	Out.clear(); if (!Filename || !*Filename) return 0;
	int N = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, reinterpret_cast<const wchar_t*>(Filename), -1, nullptr, 0, nullptr, nullptr);
	if (!N) return 0;
	Out.resize(static_cast<size_t>(N));
	if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, reinterpret_cast<const wchar_t*>(Filename), -1, &Out[0], N, nullptr, nullptr)) { Out.clear(); return 0; }
	Out.resize(static_cast<size_t>(N - 1));
	for (char& C : Out) if (C == '\\') C = '/';
	return 1;
}
CORE_API FILE* appFopen(const TCHAR* Filename, const ANSICHAR* Mode)
{
	if (!Filename || !Mode) return nullptr;
	std::wstring WMode; while (*Mode) WMode.push_back(static_cast<unsigned char>(*Mode++));
	return _wfopen(reinterpret_cast<const wchar_t*>(Filename), WMode.c_str());
}
void* appGetDllHandle(const TCHAR* Filename)
{
#if __STATIC_LINK
	(void)Filename; return &GHP2StaticLinkHandle;
#else
	HMODULE H = LoadLibraryW(reinterpret_cast<const wchar_t*>(Filename));
	if (!H) { std::wstring Name = Wide(Filename); Name += L".dll"; H = LoadLibraryW(Name.c_str()); }
	return H;
#endif
}
#if __STATIC_LINK
void appFreeDllHandle(void* Handle) { (void)Handle; }
void* appGetDllExport(void* Handle, const TCHAR* Name) { (void)Handle; (void)Name; return nullptr; }
#else
void appFreeDllHandle(void* Handle) { if (Handle) FreeLibrary(static_cast<HMODULE>(Handle)); }
void* appGetDllExport(void* Handle, const TCHAR* Name)
{
	if (!Handle || !Name) return nullptr;
	char Symbol[512]; size_t I=0;
	for (; Name[I] && I<sizeof(Symbol)-1; ++I) { if (Name[I]>0x7f) return nullptr; Symbol[I]=static_cast<char>(Name[I]); }
	Symbol[I]=0; return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Handle),Symbol));
}
#endif
void appDebugBreak() { __builtin_debugtrap(); }
void appRequestExit(UBOOL Force) { if (Force) ExitProcess(1); GIsRequestingExit = 1; }
void* appCreateProc(const TCHAR* URL, const TCHAR* Params, UBOOL)
{
	std::wstring Command = L"\"" + Wide(URL) + L"\"";
	if (Params && *Params) { Command += L" "; Command += Wide(Params); }
	STARTUPINFOW SI = {};
	SI.cb = sizeof(SI);
	PROCESS_INFORMATION ProcessInfo = {};
	if (!CreateProcessW(nullptr, &Command[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &SI, &ProcessInfo)) return nullptr;
	CloseHandle(ProcessInfo.hThread);
	return ProcessInfo.hProcess;
}
UBOOL appGetProcReturnCode(void* Handle, INT* Code)
{
	if (!Handle || !Code || WaitForSingleObject(static_cast<HANDLE>(Handle), 0) != WAIT_OBJECT_0) return 0;
	DWORD Result = 0; if (!GetExitCodeProcess(static_cast<HANDLE>(Handle), &Result)) return 0;
	*Code = static_cast<INT>(Result); CloseHandle(static_cast<HANDLE>(Handle)); return 1;
}
CORE_API const TCHAR* appTimestamp()
{
	static TCHAR Result[128]; SYSTEMTIME T; GetLocalTime(&T);
	_swprintf(Result, TEXT("%04u-%02u-%02u %02u:%02u:%02u"), T.wYear,T.wMonth,T.wDay,T.wHour,T.wMinute,T.wSecond); return Result;
}
CORE_API DWORD appGetTime(const TCHAR* Filename)
{
	WIN32_FILE_ATTRIBUTE_DATA D; if (!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(Filename), GetFileExInfoStandard, &D)) return 0;
	ULARGE_INTEGER T; T.HighPart=D.ftLastWriteTime.dwHighDateTime; T.LowPart=D.ftLastWriteTime.dwLowDateTime;
	return static_cast<DWORD>(T.QuadPart / 10000000ULL - 11644473600ULL);
}
CORE_API FTime appSecondsSlow() { return Elapsed(); }
#if !DEFINED_appSeconds
CORE_API FTime appSeconds() { return Elapsed(); }
#endif
#if !DEFINED_appCycles
CORE_API DWORD appCycles() { return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::nanoseconds>(FClock::now()-GStart).count()); }
#endif
CORE_API FTime appProcessSeconds()
{
	FILETIME C,E,K,U; if (!GetProcessTimes(GetCurrentProcess(),&C,&E,&K,&U)) return FTime();
	ULARGE_INTEGER A,B; A.HighPart=K.dwHighDateTime; A.LowPart=K.dwLowDateTime; B.HighPart=U.dwHighDateTime; B.LowPart=U.dwLowDateTime;
	return static_cast<double>(A.QuadPart+B.QuadPart)/10000000.0;
}
CORE_API void appSystemTime(INT& Y,INT& M,INT& W,INT& D,INT& H,INT& Min,INT& S,INT& Ms)
{ SYSTEMTIME T; GetLocalTime(&T); Y=T.wYear; M=T.wMonth; W=T.wDayOfWeek; D=T.wDay; H=T.wHour; Min=T.wMinute; S=T.wSecond; Ms=T.wMilliseconds; }
CORE_API void appSleep(FLOAT Seconds) { if (Seconds > 0) Sleep(static_cast<DWORD>(Seconds * 1000.f)); }
void appLaunchURL(const TCHAR* URL,const TCHAR*,FString* Error)
{ if ((INT_PTR)ShellExecuteW(nullptr,L"open",reinterpret_cast<const wchar_t*>(URL),nullptr,nullptr,SW_SHOWNORMAL) <= 32 && Error) *Error=TEXT("Unable to launch URL"); }
CORE_API void appCleanFileCache() {}
void appGetGUID(void* Out) { GUID G; if (Out && SUCCEEDED(CoCreateGuid(&G))) memcpy(Out,&G,16); else if (Out) memset(Out,0,16); }
CORE_API FGuid appCreateGuid() { FGuid G; appGetGUID(&G); return G; }
CORE_API void appClipboardCopy(const TCHAR* Text) { char Utf8[32768]; if (appToUtf8InPlace(Utf8,Text,ARRAY_COUNT(Utf8))) SDL_SetClipboardText(Utf8); }
CORE_API FString appClipboardPaste() { char* Text=SDL_GetClipboardText(); TCHAR Buffer[8192]; Buffer[0]=0; appFromUtf8InPlace(Buffer,Text ? Text : "",ARRAY_COUNT(Buffer)); FString R(Buffer); if (Text) SDL_free(Text); return R; }
CORE_API void appSetBaseDir(const TCHAR* Dir) { SetRoot(GBaseDir,Dir); }
CORE_API void appSetUserDir(const TCHAR* Dir) { SetRoot(GUserDir,Dir); }
CORE_API const TCHAR* appBaseDir() { if (!GBaseDir[0]) { wchar_t B[32768]; DWORD N=GetCurrentDirectoryW(32768,B); if (N && N<32768) { TCHAR T[32768]; CopyWide(T,B); SetRoot(GBaseDir,T); } } return GBaseDir; }
CORE_API const TCHAR* appUserDir() { return GUserDir; }
CORE_API const TCHAR* appComputerName() { static TCHAR B[MAX_COMPUTERNAME_LENGTH+1]; DWORD N=MAX_COMPUTERNAME_LENGTH+1; if (!GetComputerNameW(reinterpret_cast<wchar_t*>(B),&N)) B[0]=0; return B; }
CORE_API const TCHAR* appUserName() { static TCHAR B[UNLEN+1]; DWORD N=UNLEN+1; if (!GetUserNameW(reinterpret_cast<wchar_t*>(B),&N)) B[0]=0; return B; }
void appPlatformPreInit() { GTimestamp=1; GSecondsPerCycle=1.0e-9f; }
void appPlatformInit()
{
	WSADATA Data; WSAStartup(MAKEWORD(2,2),&Data);
	GSys=new USystem;
	GSys->LoadConfig();
	if(GSys->Paths.Num()<=5) appErrorf(TEXT("Core.System Paths did not load from System.ini"));
	GSys->AddToRoot();
	for(INT I=0;I<GSys->Suppress.Num();++I) GSys->Suppress(I).SetFlags(RF_Suppress);
	if(GWindowsDiagnosticRngSeeded)
		appSetRandTraceDiagnosticSeed(GWindowsDiagnosticRngRequestedSeed,GWindowsDiagnosticRngEffectiveSeed);
	else
		srand(static_cast<unsigned>(time(NULL)));
	debugf(NAME_Init,TEXT("Steady timer frequency=%f MHz"),0.000001/GSecondsPerCycle);
}
void appPlatformPreExit() {}
void appPlatformExit() { WSACleanup(); }
void appEnableFastMath(UBOOL) {}
char* appWindowsPath(const char* Path) { static char Buffer[4096]; strncpy(Buffer,Path ? Path : "",sizeof(Buffer)-1); Buffer[sizeof(Buffer)-1]=0; for(char* P=Buffer;*P;++P) if(*P=='/') *P='\\'; return Buffer; }
DWORD appGetLocalIP()
{
	char Host[256]; if (gethostname(Host,sizeof(Host)) != 0) return 0;
	addrinfo Hints={}; Hints.ai_family=AF_INET; addrinfo* Info=nullptr;
	if (getaddrinfo(Host,nullptr,&Hints,&Info)!=0) return 0;
	DWORD Result=0; for(addrinfo* I=Info;I;I=I->ai_next) { auto* A=reinterpret_cast<sockaddr_in*>(I->ai_addr); if (A->sin_addr.s_addr) { Result=A->sin_addr.s_addr; break; } } freeaddrinfo(Info); return Result;
}
int stricmp(const char* A,const char* B) { return _stricmp(A,B); }
int strnicmp(const char* A,const char* B,int N) { return _strnicmp(A,B,N); }
char* strupr(char* S) { return _strupr(S); }

CORE_API const TCHAR* appPackage()
{
	static TCHAR Name[MAX_PATH];
	static bool Ready=false;
	if (!Ready)
	{
		wchar_t Path[MAX_PATH]; DWORD N=GetModuleFileNameW(NULL,Path,MAX_PATH);
		if (!N || N>=MAX_PATH) Path[0]=0;
		const wchar_t* Leaf=wcsrchr(Path,L'\\'); Leaf=Leaf?Leaf+1:Path;
		wcsncpy(reinterpret_cast<wchar_t*>(Name),Leaf,MAX_PATH-1);
		wchar_t* Dot=wcsrchr(reinterpret_cast<wchar_t*>(Name),L'.'); if(Dot)*Dot=0;
		Ready=true;
	}
	return Name;
}

jmp_buf __Context::Env;
void __Context::StaticInit()
{
	const char* Text=getenv("HP2_RNG_SEED"); unsigned Requested=0,Effective=0;
	switch(HP2PrepareDiagnosticRngSeed(Text,&Requested,&Effective))
	{
		case HP2DiagnosticRngSeedInvalid:
			fprintf(stderr,"Invalid HP2_RNG_SEED: expected an unsigned decimal value from 0 to %u\n",(unsigned)RAND_MAX); exit(1);
		case HP2DiagnosticRngSeedAccepted:
			GWindowsDiagnosticRngRequestedSeed=Requested; GWindowsDiagnosticRngEffectiveSeed=Effective;
			GWindowsDiagnosticRngSeeded=1; srand(Effective); break;
		default: break;
	}
	signal(SIGSEGV,HandleSignal); signal(SIGILL,HandleSignal); signal(SIGFPE,HandleSignal);
	signal(SIGABRT,HandleSignal); signal(SIGTERM,HandleSignal);
}
void __Context::HandleSignal(int Signal) { ::HandleSignal(Signal); }
void HandleSignal(int Signal)
{
	static volatile LONG Handling=0;
	if(InterlockedExchange(&Handling,1)) ExitProcess(128+Signal);
	// Do not longjmp through an unknown or already-unwound engine context.
	ExitProcess(128+Signal);
}
void HandleInterrupt(int Signal) { HandleSignal(Signal); }
