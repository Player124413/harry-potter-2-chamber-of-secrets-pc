/*=============================================================================
	FFileManagerNameCodec.h: archive filename encoding shared by native managers.
=============================================================================*/
#ifndef INC_FFILEMANAGERNAMECODEC_H
#define INC_FFILEMANAGERNAMECODEC_H

static UBOOL EncodeArchiveName(const TCHAR* Source, UNICHAR* Wire, INT Capacity, INT& Units)
{
	if (!Source || !Wire || Capacity <= 0) return 0;
	Units = 0;
	for (INT I=0; Source[I]; ++I)
	{
		DWORD C=static_cast<DWORD>(Source[I]);
		if (sizeof(TCHAR)==sizeof(UNICHAR))
		{
			if (C>=0xd800u && C<=0xdbffu)
			{
				DWORD L=static_cast<DWORD>(Source[++I]);
				if (L<0xdc00u || L>0xdfffu || Units>Capacity-3) return 0;
				Wire[Units++]=static_cast<UNICHAR>(C); Wire[Units++]=static_cast<UNICHAR>(L); continue;
			}
			if (C>=0xdc00u && C<=0xdfffu) return 0;
		}
		else if (C>=0xd800u && C<=0xdfffu) return 0;
		if (C<=0xffffu) { if (Units>Capacity-2) return 0; Wire[Units++]=static_cast<UNICHAR>(C); }
		else if (C<=0x10ffffu) { if (Units>Capacity-3) return 0; C-=0x10000u; Wire[Units++]=static_cast<UNICHAR>(0xd800u+(C>>10)); Wire[Units++]=static_cast<UNICHAR>(0xdc00u+(C&0x3ffu)); }
		else return 0;
	}
	Wire[Units++]=0; return 1;
}
static UBOOL DecodeArchiveName(const UNICHAR* Wire, INT Units, TCHAR* Dest, INT Capacity)
{
	if (!Wire || !Dest || Units<=0 || Wire[Units-1]!=0 || Capacity<=0) return 0;
	INT N=0;
	for (INT I=0; I<Units-1; ++I)
	{
		DWORD C=static_cast<DWORD>(Wire[I]);
		if (C>=0xd800u && C<=0xdbffu)
		{
			if (I+1>=Units-1) return 0;
			DWORD L=static_cast<DWORD>(Wire[++I]); if (L<0xdc00u || L>0xdfffu) return 0;
			if (sizeof(TCHAR)==sizeof(UNICHAR)) { if(N>Capacity-3)return 0; Dest[N++]=static_cast<TCHAR>(C); Dest[N++]=static_cast<TCHAR>(L); continue; }
			C=0x10000u+((C-0xd800u)<<10)+(L-0xdc00u);
		}
		else if (C>=0xdc00u && C<=0xdfffu) return 0;
		if (N>Capacity-2) return 0; Dest[N++]=static_cast<TCHAR>(C);
	}
	Dest[N]=0; return 1;
}
#endif
