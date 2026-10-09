#include "TbFile.h"

//*************************************************************************************************************
// Define some default file operations for the windows version.
//*************************************************************************************************************

#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

static int DecodeFileHandle(int handle)
{
	return handle - 1;
}

//*************************************************************************************************************

int Default_FileOpen(const char* name, unsigned int mode)
{
#if defined(_WIN32)
	const int file = _open(name, _O_RDONLY | _O_BINARY);
	return file < 0 ? 0 : file + 1;
#else
	const int file = open(name, O_RDONLY);
	return file < 0 ? 0 : file + 1;
#endif
}

//*************************************************************************************************************

void Default_FileClose(int handle)
{
#if defined(_WIN32)
	_close(DecodeFileHandle(handle));
#else
	close(DecodeFileHandle(handle));
#endif
}

//*************************************************************************************************************

int Default_FileRead(int handle, void* buffer, int length, bool blocking)
{
	if (length < 0)
		return -1;
#if defined(_WIN32)
	return _read(DecodeFileHandle(handle), buffer, static_cast<unsigned int>(length));
#else
	const ssize_t result = read(DecodeFileHandle(handle), buffer, static_cast<size_t>(length));
	if (result < 0 || result > INT_MAX)
		return -1;
	return static_cast<int>(result);
#endif
}

//*************************************************************************************************************
// on success, return 0, failure, return non 0 (according to fseek)

int Default_FileSeek(int handle, int offset, int mode)
{
#if defined(_WIN32)
	return _lseek(DecodeFileHandle(handle), offset, mode) < 0 ? -1 : 0;
#else
	return lseek(DecodeFileHandle(handle), static_cast<off_t>(offset), mode) == static_cast<off_t>(-1) ? -1 : 0;
#endif
}
//*************************************************************************************************************

int Default_FileGetPos(int handle)
{
#if defined(_WIN32)
	const long pos = _lseek(DecodeFileHandle(handle), 0, SEEK_CUR);
	if (pos < 0 || pos > INT_MAX)
		return -1;
	return static_cast<int>(pos);
#else
	const off_t pos = lseek(DecodeFileHandle(handle), 0, SEEK_CUR);
	if (pos < 0 || pos > INT_MAX)
		return -1;
	return static_cast<int>(pos);
#endif
}

int	Default_FileGetSize(int handle)
{
	int pos = Default_FileGetPos( handle );

	Default_FileSeek( handle, 0, SEEK_END );

	int ret = Default_FileGetPos( handle );

	Default_FileSeek( handle, pos, SEEK_SET );

	return ret;
}

//*************************************************************************************************************

OPENCALLBACK	TbFile_Open = Default_FileOpen;
CLOSECALLBACK	TbFile_Close = Default_FileClose;
READCALLBACK	TbFile_Read = Default_FileRead;
SEEKCALLBACK	TbFile_Seek = Default_FileSeek;
GETSIZECALLBACK	TbFile_GetSize = Default_FileGetSize;
GETPOSCALLBACK	TbFile_GetPos = Default_FileGetPos;
FLUSHCALLBACK	TbFile_Flush = 0;
MAPFILECALLBACK	TbFile_MapFile = 0;

//*************************************************************************************************************

void TbFile_SetFileCallbacks(
	OPENCALLBACK	open,
	CLOSECALLBACK	close,
	READCALLBACK	read,
	SEEKCALLBACK	seek,
	GETSIZECALLBACK	getsize,
	GETPOSCALLBACK	getpos,
	MAPFILECALLBACK	mapfile,
	FLUSHCALLBACK	flush
)
{
	TbFile_Open		= open;
	TbFile_Close	= close;
	TbFile_Read		= read;
	TbFile_Seek		= seek;
	TbFile_GetSize	= getsize;
	TbFile_GetPos	= getpos;
	TbFile_MapFile	= mapfile;
	TbFile_Flush	= flush;
}

//*************************************************************************************************************

TbFile::TbFile() : mHandle(0),mRefCount(0),mpMappedFile(0)
{
}

//*************************************************************************************************************

TbFile::~TbFile()
{
	Close();
}

//*************************************************************************************************************

int TbFile::Open(const char *name, unsigned int mode)
{
	if (mHandle)
		return mHandle;

	if (TbFile_Open)
	{
		if( (mHandle = TbFile_Open(name, mode)) )
		{
			++mRefCount;

			return mHandle;
		}
	}
	
	return 0;
}

//*************************************************************************************************************

void TbFile::Close()
{
	if (TbFile_Close && mHandle )
	{
		if( (--mRefCount) <= 0 )
		{
			TbFile_Close( mHandle );

			if( mpMappedFile )
			{
				delete [] mpMappedFile;
				//AudioFree( mpMappedFile );
				mpMappedFile = 0;
			}

			mHandle = 0;
		}
	}
}

//*************************************************************************************************************

int TbFile::Read(void *buffer, int length, bool blocking)
{
	if (TbFile_Read && mHandle)
		return TbFile_Read(mHandle, buffer, length, blocking);
	
	return 0;
}

//*************************************************************************************************************

// on success, return 0, failure, return non 0 (according to fseek)
int TbFile::Seek(int offset, int mode)
{
	if (TbFile_Seek && mHandle)
		return TbFile_Seek(mHandle, offset, mode);

	return -1;
}

//*************************************************************************************************************

int TbFile::GetSize()
{
	if (TbFile_GetSize && mHandle)
		return TbFile_GetSize(mHandle);
	return 0;
}

//*************************************************************************************************************

int TbFile::GetPos(void)
{
	if (TbFile_GetPos && mHandle)
		return TbFile_GetPos(mHandle);
	return 0;
}

//*************************************************************************************************************

char const * TbFile::MapFile()
{ 
	if( TbFile_GetSize && TbFile_Read && mHandle )
	{
		int len = GetSize();

//		mpMappedFile = (char*)AudioAlloc( len, "TbFile::mpMappedFile" );
		mpMappedFile = new char [ len ];

		Read( mpMappedFile, len );

		return mpMappedFile;
	}

	return 0;
}

//*************************************************************************************************************

bool TbFile::Flush()
{
	if (TbFile_Flush && mHandle)
		return TbFile_Flush(mHandle);

	return true;
}