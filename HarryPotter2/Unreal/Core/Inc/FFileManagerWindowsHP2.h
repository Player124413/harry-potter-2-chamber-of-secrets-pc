/*=============================================================================
	FFileManagerWindowsHP2.h: UTF-16 Windows x64 file manager.
=============================================================================*/
#ifndef INC_FFILEMANAGERWINDOWSHP2_H
#define INC_FFILEMANAGERWINDOWSHP2_H
#include <cstdio>
#include <windows.h>
#include <shlwapi.h>
#include <io.h>
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#include "FFileManagerGeneric.h"
#include "FFileManagerNameCodec.h"

class FArchiveFileReaderWindowsHP2 : public FArchive
{
public:
	FArchiveFileReaderWindowsHP2(FILE* F, FOutputDevice* E, INT S):File(F),Error(E?E:GNull),Size(S),Pos(0){ArIsLoading=ArIsPersistent=1;}
	~FArchiveFileReaderWindowsHP2(){if(File)Close();}
	void Seek(INT P){if(P<0||P>Size||_fseeki64(File,P,SEEK_SET)!=0){ArIsError=1;return;}Pos=P;}
	INT Tell(){return Pos;} INT TotalSize(){return Size;}
	UBOOL Close(){if(File){if(fclose(File)!=0)ArIsError=1;File=NULL;}return !ArIsError;}
	void Serialize(void* V,INT N){if(N<0||Pos+N>Size||fread(V,1,N,File)!=(size_t)N){ArIsError=1;return;}Pos+=N;}
	FArchive& operator<<(FName& Name){UNICHAR Wire[NAME_SIZE];TCHAR Text[NAME_SIZE];INT Bytes=0;ByteOrderSerialize(&Bytes,4);if(Bytes<=0||Bytes%2||Bytes>(INT)sizeof(Wire)){ArIsError=1;return *this;}INT Units=Bytes/2;for(INT I=0;I<Units;++I)ByteOrderSerialize(&Wire[I],2);if(!ArIsError&&DecodeArchiveName(Wire,Units,Text,NAME_SIZE))Name=FName(Text);else ArIsError=1;return *this;}
private: FILE* File; FOutputDevice* Error; INT Size,Pos;
};
class FArchiveFileWriterWindowsHP2 : public FArchive
{
public:
	FArchiveFileWriterWindowsHP2(FILE* F,FOutputDevice* E,INT P):File(F),Error(E?E:GNull),Pos(P){ArIsSaving=ArIsPersistent=1;}
	~FArchiveFileWriterWindowsHP2(){if(File)Close();}
	void Seek(INT P){if(P<0||_fseeki64(File,P,SEEK_SET)!=0){ArIsError=1;return;}Pos=P;}
	INT Tell(){return Pos;} UBOOL Close(){if(File){if(fflush(File)||fclose(File))ArIsError=1;File=NULL;}return !ArIsError;}
	void Serialize(void* V,INT N){if(N<0||Pos+N>MAXINT||fwrite(V,1,N,File)!=(size_t)N){ArIsError=1;return;}Pos+=N;}
	FArchive& operator<<(FName& Name){UNICHAR Wire[NAME_SIZE];INT Units=0;if(!EncodeArchiveName(*Name,Wire,NAME_SIZE,Units)){ArIsError=1;return *this;}INT Bytes=Units*2;ByteOrderSerialize(&Bytes,4);for(INT I=0;I<Units&&!ArIsError;++I)ByteOrderSerialize(&Wire[I],2);return *this;}
private: FILE* File; FOutputDevice* Error; INT Pos;
};
class FFileManagerWindowsHP2 : public FFileManagerGeneric
{
public:
	FArchive* CreateFileReader(const TCHAR* F,DWORD Flags=0,FOutputDevice* Error=GNull){FILE* H=appFopen(F,"rb");if(!H){if(Flags&FILEREAD_NoFail)appErrorf(TEXT("Failed to read file: %s"),F);return NULL;}if(_fseeki64(H,0,SEEK_END)){fclose(H);return NULL;}__int64 S=_ftelli64(H);if(S<0||S>MAXINT||_fseeki64(H,0,SEEK_SET)){fclose(H);return NULL;}return new(TEXT("WindowsHP2Reader")) FArchiveFileReaderWindowsHP2(H,Error,(INT)S);}
	FArchive* CreateFileWriter(const TCHAR* F,DWORD Flags=0,FOutputDevice* Error=GNull){if(Flags&FILEWRITE_EvenIfReadOnly)SetFileAttributesW((const wchar_t*)F,FILE_ATTRIBUTE_NORMAL);const wchar_t* Mode=(Flags&FILEWRITE_Append)?L"ab":(Flags&FILEWRITE_NoReplaceExisting)?L"wbx":L"wb";FILE* H=_wfopen((const wchar_t*)F,Mode);if(!H){if(Flags&FILEWRITE_NoFail)appErrorf(TEXT("Failed to create file: %s"),F);return NULL;}if(Flags&FILEWRITE_Unbuffered)setvbuf(H,NULL,_IONBF,0);INT P=0;if(Flags&FILEWRITE_Append){_fseeki64(H,0,SEEK_END);__int64 Pos=_ftelli64(H);if(Pos<0||Pos>MAXINT){fclose(H);return NULL;}P=(INT)Pos;}return new(TEXT("WindowsHP2Writer")) FArchiveFileWriterWindowsHP2(H,Error,P);}
	INT FileSize(const TCHAR* F){WIN32_FILE_ATTRIBUTE_DATA D;if(!GetFileAttributesExW((const wchar_t*)F,GetFileExInfoStandard,&D))return -1;ULARGE_INTEGER S;S.HighPart=D.nFileSizeHigh;S.LowPart=D.nFileSizeLow;return S.QuadPart>MAXINT?-1:(INT)S.QuadPart;}
	UBOOL Copy(const TCHAR* D,const TCHAR* S,UBOOL Replace=1,UBOOL EvenReadOnly=0,UBOOL Attributes=0,void (*Progress)(FLOAT)=NULL){return FFileManagerGeneric::Copy(D,S,Replace,EvenReadOnly,Attributes,Progress);}
	UBOOL Delete(const TCHAR* F,UBOOL Required=0,UBOOL EvenReadOnly=0){if(EvenReadOnly)SetFileAttributesW((const wchar_t*)F,FILE_ATTRIBUTE_NORMAL);return DeleteFileW((const wchar_t*)F)||(!Required&&GetLastError()==ERROR_FILE_NOT_FOUND);}
	UBOOL Move(const TCHAR* D,const TCHAR* S,UBOOL Replace=1,UBOOL EvenReadOnly=0,UBOOL Attributes=0){if(EvenReadOnly)SetFileAttributesW((const wchar_t*)S,FILE_ATTRIBUTE_NORMAL);DWORD Flags=MOVEFILE_COPY_ALLOWED|(Replace?MOVEFILE_REPLACE_EXISTING:0);if(!MoveFileExW((const wchar_t*)S,(const wchar_t*)D,Flags))return 0;if(!Attributes)SetFileAttributesW((const wchar_t*)D,FILE_ATTRIBUTE_NORMAL);return 1;}
	UBOOL MakeDirectory(const TCHAR* P,UBOOL Tree=0){if(Tree)return FFileManagerGeneric::MakeDirectory(P,1);return CreateDirectoryW((const wchar_t*)P,NULL)||GetLastError()==ERROR_ALREADY_EXISTS;}
	UBOOL DeleteDirectory(const TCHAR* P,UBOOL Required=0,UBOOL Tree=0){if(Tree)return FFileManagerGeneric::DeleteDirectory(P,Required,1);return RemoveDirectoryW((const wchar_t*)P)||(!Required&&GetLastError()==ERROR_PATH_NOT_FOUND);}
	TArray<FString> FindFiles(const TCHAR* Spec,UBOOL Files,UBOOL Dirs){TArray<FString> R;WIN32_FIND_DATAW D;HANDLE H=FindFirstFileW((const wchar_t*)Spec,&D);if(H==INVALID_HANDLE_VALUE)return R;const wchar_t* Pattern=wcsrchr((const wchar_t*)Spec,L'\\');const wchar_t* Slash=wcsrchr((const wchar_t*)Spec,L'/');if(!Pattern||(Slash&&Slash>Pattern))Pattern=Slash;Pattern=Pattern?Pattern+1:(const wchar_t*)Spec;do{if(!wcscmp(D.cFileName,L".")||!wcscmp(D.cFileName,L"..")||!PathMatchSpecW(D.cFileName,Pattern))continue;UBOOL IsDir=(D.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;if((IsDir&&Dirs)||(!IsDir&&Files))new(R)FString((const TCHAR*)D.cFileName);}while(FindNextFileW(H,&D));FindClose(H);return R;}
	UBOOL SetDefaultDirectory(const TCHAR* P){return SetCurrentDirectoryW((const wchar_t*)P);}
	FString GetDefaultDirectory(){TCHAR B[32768];DWORD N=GetCurrentDirectoryW(ARRAY_COUNT(B),(wchar_t*)B);return N&&N<ARRAY_COUNT(B)?FString(B):FString(TEXT(""));}
	SQWORD GetGlobalTime(const TCHAR* F){WIN32_FILE_ATTRIBUTE_DATA D;if(!GetFileAttributesExW((const wchar_t*)F,GetFileExInfoStandard,&D))return 0;ULARGE_INTEGER T;T.HighPart=D.ftLastWriteTime.dwHighDateTime;T.LowPart=D.ftLastWriteTime.dwLowDateTime;return T.QuadPart/10000000ULL-11644473600ULL;}
	UBOOL SetGlobalTime(const TCHAR* F){HANDLE H=CreateFileW((const wchar_t*)F,FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(H==INVALID_HANDLE_VALUE)return 0;FILETIME T;GetSystemTimeAsFileTime(&T);BOOL Ok=SetFileTime(H,NULL,NULL,&T);CloseHandle(H);return Ok;}
};
#endif
