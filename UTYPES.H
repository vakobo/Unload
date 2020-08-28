#ifndef	__UTYPES__
#define	__UTYPES__

#undef FAR
//#define FAR _far
#define	FAR far


#if !defined(VOID)
typedef void	VOID;
typedef	VOID*	PVOID;
typedef	VOID	FAR*	LPVOID;
#endif

#if !defined(BYTE)
typedef unsigned char	BYTE;
typedef	BYTE*	PBYTE;
typedef	BYTE	FAR*	LPBYTE;
#endif

#if !defined(WORD)
typedef unsigned short	WORD;
typedef	WORD*	PWORD;
typedef	WORD	FAR*	LPWORD;
#endif

#if !defined(DWORD)
typedef unsigned long	DWORD;
typedef	DWORD*	PDWORD;
typedef	DWORD	FAR*	LPDWORD;
#endif

//#if !defined(BOOL)
//typedef unsigned short	BOOL;
//#define	False 0
//#define	True 1
//#endif

#if !defined(LPSTR)
typedef char FAR	*LPSTR;
#endif

#if !defined(LPCSTR)
typedef const char FAR	*LPCSTR;
#endif

#endif	//__UTYPES__
