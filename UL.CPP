#include <dos.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <mem.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <sys\stat.h>

#include "utypes.h"

WORD	wLolOff;
WORD	wLolSeg;

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
/*
typedef	struct	tagLoL
{
	WORD	wSegMCB;
//00
	DWORD	dwDPB;
	DWORD	dwFT1;
	DWORD	dwClock;
	DWORD	dwCon;
	WORD	wMaxSectorSize;
	DWORD	dwCacheHead;
//16h
	DWORD	dwCDS;
	DWORD	dwFCB;
	WORD	wFcbSize;
//20h
	BYTE	bDrvCnt;
	BYTE	bLastDrv;
//22h
	TDH	tdhNul;
}	TLoL;
typedef	TLoL	FAR*	LPLoL;
*/

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


WORD	SearchMCB(const char* aName)
{
	WORD	wMCB;
	LPMCB	curMCB;

	wMCB	= *(LPWORD)MK_FP(wLolSeg, wLolOff - 2);
	curMCB	= (LPMCB)MK_FP(wMCB, 0);
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

void	FreeMCB(WORD wMcb)
{
	LPMCB	pmcb;
	LPMCB	pmcbn;
	WORD	wNext;

	pmcb	= (LPMCB)MK_FP(wMcb, 0);
	wNext	= wMcb + pmcb->wSize + 1;
	pmcbn	= (LPMCB)MK_FP(wNext, 0);
	if(pmcbn->wOwner == 0)
	{
		fprintf(stdout, " Unite MCB's %04X & %04X\n", wMcb, wNext);
		pmcb->wSize	+= pmcbn->wSize + 1;
		pmcb->bSig	= pmcbn->bSig;	//may be it is 'Z'
		memset(pmcbn, 0, 16);
	}
	pmcb->wOwner	= 0;
}

void	ParseDeviceDrivers(WORD wTarget)
{
	WORD	wSeg;
	LPDH	pDH;
	LPDH	next;

	pDH	= (LPDH)MK_FP(wLolSeg, wLolOff + 0x22);

//-	First driver is NUL
GoNext:
	next	= (LPDH)pDH->dwNext;
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
		fprintf(stderr, "DEVLOAD DI1000DD.SYS = %d\n", rv);
	}
}

int	(*XMS)() = NULL;

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

int	RestoreInt(int num, WORD wPSP, WORD wOff, char* name)
{
	LPDWORD	lpDW;
	LPDWORD	lpIT;
	DWORD	dw;

	lpDW	= (LPDWORD)MK_FP(wPSP, wOff);
	fprintf(stdout, " Saved by %s int %02Xh at %04X:%04X = %08lX\n", name, num, wPSP, wOff, *lpDW);
	if(*lpDW)
	{
		lpIT	= NULL;
		dw	= lpIT[num];
		fprintf(stdout, " Current int %02Xh = %08lX\n", num, dw);
		if((WORD)(dw >> 16) == wPSP)
		{
			fprintf(stdout, " Restore int %02Xh\n", num);
			asm	cli
			lpIT[num]	= *lpDW;
			asm	sti
			fprintf(stdout, " Int %02Xh restoration complete.\n", num);

			return	1;
		}

		fprintf(stdout, " Current int %02Xh dosn't match %s PSP", num, name);
	}
	else
	{
		fprintf(stdout, " %s dosn't replace int %02Xh", name, num);
	}

	fprintf(stdout, ". Skip restoration.\n");

	return	0;
}


void	FreeXmsHandles(WORD wSeg, WORD wOff)
{
	int	c;
//	WORD	wAX;
	WORD	wBX;
	WORD	wDX;
	WORD	wLC;
	WORD	hx;
	LPDWORD	lpXR;

	lpXR	= (LPDWORD)MK_FP(wSeg, wOff);
	if(*lpXR)
	{
		fprintf(stdout, " Saved XMS driver at %04X = %08lX\n", wOff, *lpXR);
		fprintf(stdout, " Looking for allocated EMB's ...\n");
	}
	else
	{
		fprintf(stdout, " XMS driver not set!\n");
		return;
	}

	if(*lpXR == (DWORD)XMS)
	{
		wOff	+= 4;
	}
	else
	{
		fprintf(stdout, " XMS Routine not match! Can't free XMS handles!\n");
		return;
	}
	hx	= *(LPWORD)MK_FP(wSeg, wOff);

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
				fprintf(stderr, " Free handle error = %2.2X (handle = %4.4X)\n", wBX & 255, hx);
			}
		}
		else
		{
			wBX	= _BX;
			fprintf(stderr, " Get handle information error = %2.2X (handle = %4.4X)\n", wBX & 255, hx);
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

void	UnloadDI1k(WORD wMCB)
{
	WORD	wPSP;
	WORD	dc,i,j;
	LPDH	lpDevHdr;
	LPCDS	lpCurDir;	//Current Directory
	LPDPB	lpDrvPrm;	//Drive Parameters
	LPBYTE	lpDC;

	wPSP	= mcb2psp(wMCB);
//-	get number of devices controlled by DI1000DD.SYS
	lpDevHdr	= (LPDH)MK_FP(wPSP, 0);
	dc	= (WORD)lpDevHdr->abName[0];
	fprintf(stdout, "DI1000DD MCB=%04X PSP=%04X NoD=%d\n", wMCB, wPSP, dc);

//-	Restore interrupt vector 13h
	if(RestoreInt(0x13, wPSP, 0x61, "DI1000DD"))
	{

//-		decrement number of drives in BIOS Data Area
		*(LPBYTE)0x0475L	-= dc;

//-		decrement number of drives in LoL
		lpDC	= (LPBYTE)MK_FP(wLolSeg, wLolOff + 0x20);
		fprintf(stdout, " DrvCnt before decrement = %d\n", *lpDC);// & 0xFF);
		*lpDC	-= dc;
		fprintf(stdout, " DrvCnt after  decrement = %d\n", *lpDC);// & 0xFF);

//-		clear CDS's & DPB's
		lpCurDir	= *(LPCDS*)MK_FP(wLolSeg, wLolOff + 0x16);
		fprintf(stdout, " lpCurDir = %Fp\n", lpCurDir);
		i	= *lpDC - 1;
		lpDrvPrm	= (LPDPB)lpCurDir[i].dwDPB;
		fprintf(stdout, " lpDrvPrm = %Fp\n", lpDrvPrm);
		lpDrvPrm->dwNext	= 0x0000FFFFL;
		j	= 0;
		while(j < dc)
		{
			i++;
			j++;
			lpCurDir[i].wFlags	= 0;
			lpCurDir[i].wClustNo	= 0;
			lpCurDir[i].dwDPB	= 0L;
			memset(lpCurDir[i].szPath + 3, 0, 64);
		}
	}
	else
	{
		fprintf(stdout, " Also skip other disk drives operations.\n");
	}

//-	Walk through Device drivers
	ParseDeviceDrivers(wPSP);

//-	Release memory control block
	FreeMCB(wMCB);
}

void	UnloadUA(WORD wMCB)
{
	WORD	wPSP;
	WORD	wXMS;
	WORD	w15h;
	LPDWORD	lpDW;

	wPSP	= mcb2psp(wMCB);
	fprintf(stdout, "USBASPI  MCB=%04X PSP=%04X\n", wMCB, wPSP);

	lpDW	= (LPDWORD)MK_FP(wPSP, 6);
	if(*lpDW == 0x0066005BL)
	{
		lpDW	= (LPDWORD)MK_FP(wPSP, 6);
		if(*lpDW == 0x0066005BL)
		{
			lpDW	= (LPDWORD)MK_FP(wPSP, 0x77);
			switch(*lpDW)
			{
			case 0x3D48840FL:	//v2.20
			case 0x3E00840FL:	//v2.24
				wXMS	= 0x029A;
				w15h	= 0x035F;
				break;
			case 0x3E63840FL:	//v2.27
				wXMS	= 0x0298;
				w15h	= 0x035D;
				break;
			case 0x3983840FL:	//v2.27x
				wXMS	= 0x025A;
				w15h	= 0x031E;
				break;
			default:
				fprintf(stderr, " Unknown USBASPI version (%d). Skip unload!\n", 2);
				return;
			}
		}
	}
	else
	{
		fprintf(stderr, " Unknown USBASPI version (%d). Skip unload!\n", 1);
		return;
	}

//-	Free allocated EMB's
	FreeXmsHandles(wPSP, wXMS);

//-	Restore interrupt vector 15h
	RestoreInt(0x15, wPSP, w15h, "USBASPI");

//-	Walk through Device drivers
	ParseDeviceDrivers(wPSP);

//-	Release memory control block
	FreeMCB(wMCB);
}

void	UnloadDrivers()
{
	int	cul;
//	WORD	wLolOff;
//	WORD	wLolSeg;
	WORD	wDrvMCB;
//	LPLoL	lpMain;
//	LPMCB	lpMcbList;

//0	Backup & set UMB link state
	if(XMS)
	{
		cul	= get_umb_link();
		set_umb_link(1);
	}

//1	Get LoL
	asm	push	es
	asm	push	bx
	asm	mov	ax,5200h
	asm	int	21h
	wLolOff	= _BX;
	wLolSeg	= _ES;
	asm	pop	bx
	asm	pop	es
//	lpMain	= (LPLoL)MK_FP(wLolSeg, wLolOff - 2);
//	lpMcbList	= (LPMCB)MK_FP(lpMain->wSegMCB, 0);

//2	Search DI1000DD
	wDrvMCB	= SearchMCB("DI1000DD");

//3	Unload DI1000DD.SYS
	if(wDrvMCB && wDrvMCB != 0xFFFF)
	{
		UnloadDI1k(wDrvMCB);
	}
	else
	{
		fprintf(stderr, "%s not found!\n", "DI1000DD");
	}

//4	Search USBASPI
	wDrvMCB	= SearchMCB("USBASPI");

//5	Unload USBASPI.SYS
	if(wDrvMCB && wDrvMCB != 0xFFFF)
	{
		UnloadUA(wDrvMCB);
	}
	else
	{
		fprintf(stderr, "%s not found!\n", "USBASPI");
	}

//6	Restore UMB link state
	if(XMS)
	{
		set_umb_link(cul);
	}
}

void	Dump1M(char* fn)
{
	int	fh	= open(fn, O_BINARY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE);

	WORD	i	= 0;
	do
	{
		write(fh, MK_FP(i << 8, 0), 4096);
		i++;
	}
	while(i < 256);

	close(fh);
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
			if(!strcmp(parg, "-L"))	//load drivers
			{
				opt	|= 1;
			}
			else
			if(!strcmp(parg, "-D"))	//dump
			{
				opt	|= 2;
			}
			i++;
		}
	}

	if(InitXMS())
	{
		fprintf(stderr, "%s not found!\n", "XMS");
	}

	if(opt & 2)
	{
		Dump1M("0.BIN");
	}

	if(opt & 1)
	{
		LoadDrivers();
		fprintf(stdout, "Load complete\n");
	}
	else
	{
		UnloadDrivers();
		fprintf(stdout, "Unload complete\n");
	}

	if(opt & 2)
	{
		Dump1M("1.BIN");
	}

	return	0;
}
