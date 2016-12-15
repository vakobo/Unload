#include <dos.h>
#include <errno.h>
#include <mem.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

#include "utypes.h"

//-	Memory Control Block
typedef	struct	tagMemCtrlBlk
{
	BYTE	bSig;
	WORD	wOwner;
	WORD	wSize;
	BYTE	res[3];
	char	szName[8];
}	TMCB;
typedef	TMCB	FAR*	LPMCB;

//-	Drive Parameter Block
typedef	struct	tagDrvPrmBlk
{
/*00*/	BYTE	bDrNo;	//drive number
/*01*/	BYTE	bUnNo;	//sub-unit number
/*02*/	WORD	wBpS;	//bytes per sector
/*04*/	BYTE	bClustMask;	//highest sector number within a cluster
/*05*/	BYTE	bClustShift;	//shift count to convert clusters into sectors
/*06*/	WORD	wFirstFAT;
/*08*/	BYTE	bFatCnt;
/*09*/	WORD	wRootEntries;
/*0B*/	WORD	wFirstData;
/*0D*/	WORD	wHiClust;
//-	DOS 4+
/*0F*/	WORD	wSpF;	//Sectors per FAT
/*11*/	WORD	wRootSec;	//sector # of start of root directory
/*13*/	DWORD	dwDevHdr;	//address of DevHdrRec
/*17*/	BYTE	bMedia;
/*18*/	BYTE	FirstAccess;
/*19*/	DWORD	dwNext;
/*1D*/	WORD	wNextFree;	//last allocated cluster
/*1F*/	WORD	wFreeCnt;	//number of free clusters
}	TDPB;
typedef	TDPB	FAR*	LPDPB;

//-	Current Directory Strucrure
typedef	struct	tagCurDirStruct
{
	char	szPath[67];
	WORD	wFlags;
	DWORD	dwDPB;
	WORD	wClustNo;
	DWORD	dwRes;
	WORD	wSlashOffset;
	BYTE	bRes;
	DWORD	dwIfsDrvr;
	WORD	wRes;
}	TCDS;
typedef	TCDS	FAR*	LPCDS;

//-	Device header
typedef	struct	tagDevHdr
{
	DWORD	dwNext;
	WORD	wAttr;
	WORD	wSEP;	//Strategy Entry Point
	WORD	wDIEP;	//Device Interrupt Entry Point
	BYTE	abName[8];
}	TDH;
typedef	TDH	FAR*	LPDH;

//-	MS-DOS List Of Lists structure
typedef	struct	tagLoL
{
	WORD	wSegMCB;
	DWORD	dwDPB;
	DWORD	dwFT1;
	DWORD	dwClock;
	DWORD	dwCon;
	WORD	wMaxSectorSize;
	DWORD	dwCacheHead;
	DWORD	dwCDS;
	DWORD	dwFCB;
	WORD	wFcbSize;
	BYTE	bDrvCnt;
	BYTE	bLastDrv;
	TDH	tdhNul;
}	TLoL;
typedef	TLoL	FAR*	LPLoL;


int	get_umb_link()
{
	_AX	= 0x5802;
	asm	int	21h

	return	(_AX & 255);
}

void	set_umb_link(int link)
{
	_BX	= link;
	_AX	= 0x5803;
	asm	int	21h
	asm	jnc	EndSet
	fprintf(stdout, "Error setting UMB Link!\n");
EndSet:
}


WORD	SearchMCB(LPMCB curMCB, const char* aName)
{
	WORD	wMCB;

	while(curMCB->bSig == 'M')
	{
		wMCB	= FP_SEG(curMCB);
		switch(curMCB->wOwner)
		{
		case 0:
		case 8:
			break;
		default:
			if(!strncmp(curMCB->szName, aName, 8))
			{
				return	wMCB;
			}
		}
		curMCB	= (LPMCB)MK_FP(wMCB + curMCB->wSize + 1, 0);
	}

	if(curMCB->bSig != 'Z')
	{
		fprintf(stderr, "MCB chain is corrupted\n");
		return	0xFFFF;
	}

	if(!strncmp(curMCB->szName, aName, 8))
	{
		return	wMCB;
	}

	return	0;
}

void	FreeMCB(WORD wMcbSeg)
{
	LPMCB	pmcb;
	LPMCB	pmcbn;
	WORD	wNext;

	pmcb	= (LPMCB)MK_FP(wMcbSeg, 0);
	wNext	= wMcbSeg + pmcb->wSize + 1;
	pmcbn	= (LPMCB)MK_FP(wNext, 0);
	if(pmcbn->wOwner == 0)
	{
		fprintf(stdout, " Unite MCB's %04X & %04X\n", wMcbSeg, wNext);
		pmcb->wSize	+= pmcbn->wSize + 1;
		pmcb->bSig	= pmcbn->bSig;	//may be it is 'Z'
		memset(pmcbn, 0, 16);
	}
	pmcb->wOwner	= 0;
}

void	ParseDeviceDrivers(LPVOID pdl, WORD wTarget)
{
	WORD	wSeg;
	TDH far* pDH;
	TDH far* next;

	pDH	= (TDH far*)pdl;

//-	First driver is NUL
GoNext:
	next	= (TDH far*)pDH->dwNext;
	wSeg	= FP_SEG(next);

	if(wSeg == wTarget)
	{
		pDH->dwNext	= next->dwNext;
		return;
	}

	if((pDH->dwNext & 0x0000FFFFL) != 0x0000FFFFL)
	{
		pDH	= next;
		goto	GoNext;
	}
}

void	LoadDrivers()
{
	char*	cl_dl_ua[] = {"", "/h", "USBASPI.SYS", "/e/v", ""};
	char*	cl_dl_di[] = {"", "/h", "DI1000DD.SYS", ""};
	int	rv;

//-	load USBASPI
	rv	= spawnvp(P_WAIT, "DEVLOAD.COM", cl_dl_ua);
	switch(rv)
	{
	case -1:
		fprintf(stderr, "Spawn DEVLOAD error=%d\n", errno);
		return;
	case 0:
		fprintf(stdout, "DEVLOAD USBASPI.SYS OK\n");
		break;
	default:
		fprintf(stderr, "DEVLOAD USBASPI.SYS error=%d\n", rv);
		return;
	}

//-	load DI1000DD
	rv	= spawnvp(P_WAIT, "DEVLOAD.COM", cl_dl_di);
	switch(rv)
	{
	case -1:
		fprintf(stderr, "Spawn DEVLOAD error=%d\n", errno);
		break;
	case 0:
		fprintf(stdout, "DEVLOAD DI1000DD.SYS OK\n");
		break;
	default:
		fprintf(stderr, "DEVLOAD DI1000DD.SYS error=%d\n", rv);
	}
}

int	(*XMS)();

int	InitXMS()
{
	WORD	wAX;
	WORD	wBX;

	asm	mov	ax,4300h
	asm	int	2Fh
	asm	cmp	al,80h
	asm	je	XmsOK
	return	1;
XmsOK:
	asm	mov	ax,4310h;
	asm	int	2Fh
	XMS	= (int(*)())MK_FP(_ES, _BX);

//-	Query XMS driver version
	asm	xor	ah,ah
	XMS();
	wAX	= _AX;
	wBX	= _BX;
	fprintf(stdout, " XMS driver = %Fp  version = %4.4X(%4.4X)\n", XMS, wAX, wBX);

	return	0;
}

void	FreeXmsHandles(WORD hx)
{
	int	c;
//	WORD	wAX;
	WORD	wBX;
	WORD	wDX;
	WORD	wLC;

	c	= 2;
	do
	{
//-		Query XMS handle info
		_AH	= 0x0E;
		_DX	= hx;
		if(XMS())
		{
//			wAX	= _AX;
			wBX	= _BX;
			wDX	= _DX;
			wLC	= wBX >> 8;
			fprintf(stdout, " XMS  block = %4.4X  size = %4.4XK  lock count = %d\n", hx, wDX, wLC);
//-			Unlock XMS handle
			while(wLC)
			{
				_AH	= 0x0D;
				_DX	= hx;
				XMS();
				wLC--;
			}
//-			Free XMS handle
			_AH	= 0x0A;
			_DX	= hx;
			if(XMS())
			{
			}
			else
			{
				wBX	= _BX;
				fprintf(stdout, " Free handle error = %2.2X (handle = %4.4X)\n", wBX & 255, hx);
			}
		}
		else
		{
			wBX	= _BX;
			fprintf(stdout, " Get handle information error = %2.2X (handle = %4.4X)\n", wBX & 255, hx);
		}
		hx	-=10;
		c--;
	}
	while(c > 0);

	_AH	= 0x0E;
	_DX	= hx;
	if(XMS())
	{
		wBX	= _BX;
		fprintf(stdout, " Free handles = %d\n", wBX & 0xFF);
	}
}

WORD	mcb2psp(WORD wMCB)
{
	return	((LPMCB)MK_FP(wMCB, 0))->wOwner;
}

void	UnloadDrivers()
{
	int	cul;
	WORD	dc,i,j;
	WORD	wLolOff;
	WORD	wLolSeg;
	WORD	wDrvMCB;
	WORD	wDrvPSP;
	LPDWORD	lpIT;
	LPDWORD	lp13;
	LPLoL	lpMain;
	LPMCB	lpMcbList;
	LPCDS	lpCurDir;	//Current Directory
	LPDPB	lpDrvPrm;	//Drive Parameters
	LPDH	lpDevHdr;

//0	Backup & set UMB link state
	cul	= get_umb_link();
	set_umb_link(1);

//1	Get LoL
	asm	push	es
	asm	push	bx
	asm	mov	ax,5200h
	asm	int	21h
	wLolOff	= _BX;
	wLolSeg	= _ES;
	asm	pop	bx
	asm	pop	es
	lpMain	= (LPLoL)MK_FP(wLolSeg, wLolOff - 2);
	lpMcbList	= (LPMCB)MK_FP(lpMain->wSegMCB, 0);

//2	Search DI1000DD
	wDrvMCB	= SearchMCB(lpMcbList, "DI1000DD");

//3	Unload DI1000DD.SYS
	if(wDrvMCB && wDrvMCB != 0xFFFF)
	{
//-		get number of devices controlled by DI1000DD.SYS
		wDrvPSP	= mcb2psp(wDrvMCB);
		lpDevHdr	= (LPDH)MK_FP(wDrvPSP, 0);
		dc	= (WORD)lpDevHdr->abName[0];
		fprintf(stdout, "DI1000DD MCB=%04X PSP=%04X NoD=%d\n", wDrvMCB, wDrvPSP, dc);

//-		decrement number of drives in BIOS Data Area
		*(LPBYTE)0x0475L	-= dc;

//-		restore Int 13h
		lpIT	= NULL;
		lp13	= (LPDWORD)MK_FP(wDrvPSP, 0x61);
		asm	cli
		lpIT[0x13]	= *lp13;
		asm	sti

//-		decrement number of drives in LoL
		fprintf(stdout, " DrvCnt before decrement = %d\n", lpMain->bDrvCnt & 0xFF);
		lpMain->bDrvCnt	-= dc;
		fprintf(stdout, " DrvCnt after  decrement = %d\n", lpMain->bDrvCnt & 0xFF);

//-		clear CDS's & DPB's
		lpCurDir	= (LPCDS)lpMain->dwCDS;
		i	= lpMain->bDrvCnt - 1;
		lpDrvPrm	= (LPDPB)lpCurDir[i].dwDPB;
		lpDrvPrm->dwNext	= 0x0000FFFFL;
		j	= 0;
		while(j < dc)
		{
			i++;
			j++;
			lpCurDir[i].wFlags	= 0;
			lpCurDir[i].wClustNo	= 0;
			lpCurDir[i].dwDPB	= 0L;
		}

//-		Walk through Device drivers
		ParseDeviceDrivers(MK_FP(wLolSeg, wLolOff + 0x22), wDrvPSP);

//-		Release memory control block
		FreeMCB(wDrvMCB);
	}
	else
	{
		fprintf(stdout, "DI1000DD not found!\n");
	}

//4	Search USBASPI
	wDrvMCB	= SearchMCB(lpMcbList, "USBASPI");

//5	Unload USBASPI.SYS
	if(wDrvMCB && wDrvMCB != 0xFFFF)
	{
		wDrvPSP	= mcb2psp(wDrvMCB);
		fprintf(stdout, "USBASPI  MCB=%04X PSP=%04X\n", wDrvMCB, wDrvPSP);

//-		Walk through Device drivers
		ParseDeviceDrivers(MK_FP(wLolSeg, wLolOff + 0x22), wDrvPSP);

		if(InitXMS())
		{
			fprintf(stderr, "XMS not found!\n");
		}
		else
		{
			i	= 0;
//-			Check 2.27x
			lpIT	= (LPDWORD)MK_FP(wDrvPSP, 0x025A);
			if(*lpIT == (DWORD)XMS)
			{
				i	= 0x25E;
			}
			else
			{
//-				Check 2.27
				lpIT	= (LPDWORD)MK_FP(wDrvPSP, 0x0298);
				if(*lpIT == (DWORD)XMS)
				{
					i	= 0x29C;
				}
				else
				{
//-					Check 2.20
					lpIT	= (LPDWORD)MK_FP(wDrvPSP, 0x029A);
					if(*lpIT == (DWORD)XMS)
					{
						i	= 0x29E;
					}
				}
			}

			if(i)
			{
				j	= *(LPWORD)MK_FP(wDrvPSP, i);
				fprintf(stdout, " XMS handle found at %4.4X = %4.4X!\n", i, j);
				FreeXmsHandles(j);
			}
			else
			{
				fprintf(stdout, " Unknown USBASPI version. Can't free XMS handles!\n");
			}
		}

//-		Release memory control block
		FreeMCB(wDrvMCB);
	}
	else
	{
		fprintf(stdout, "USBASPI not found!\n");
	}

//6	Restore UMB link state
	set_umb_link(cul);
}

int	main(int argc, char* argv[])
{
	int	i;
	int	opt;
	char*	parg;

	fprintf(stdout, "-=: VaKoBo's DI1000DD & USBASPI unloader :=-\n");
	opt	= 0;
	if(argc > 1)
	{
		i	= 1;
		while(i < argc)
		{
			parg	= strupr(argv[i]);
			if(!strcmp(parg, "-L"))
			{
				opt	|= 1;
			}
			i++;
		}
	}

	if(opt & 1)
	{
		LoadDrivers();
		fprintf(stdout, "Load complete\n");
	}

	UnloadDrivers();
	fprintf(stdout, "Unload complete\n");

	return	0;
}
