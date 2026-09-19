// vgui2_base.cpp - backend pointer storage + IMemAlloc singleton.
//
// The IMemAlloc part is compiled only when Valve's IMemAlloc declaration is
// visible, i.e. WITHOUT NO_MALLOC_OVERRIDE (the engine vgui2engine objects).
// It provides g_pMemAlloc, which tier1 (CUtlMemory::AllocSize) references.

#include <cstdlib>

#include "vgui2_backend.h"

static const vgui2_backend_t *s_backend = 0;

void VGui2_SetBackend( const vgui2_backend_t *backend )
{
	s_backend = backend;
}

const vgui2_backend_t *VGui2_GetBackend( void )
{
	return s_backend;
}

//-----------------------------------------------------------------------------
// Minimal malloc-backed IMemAlloc (replaces prebuilt tier0's allocator).
//-----------------------------------------------------------------------------
#if !defined( NO_MALLOC_OVERRIDE )

#include "tier0/memalloc.h"

#if defined( _WIN32 )
#include <malloc.h>
#define VGUI2_MSIZE( p ) _msize( p )
#elif defined( OSX )
#include <malloc/malloc.h>
#define VGUI2_MSIZE( p ) malloc_size( p )
#else
#include <malloc.h>
#define VGUI2_MSIZE( p ) malloc_usable_size( p )
#endif

class CBaseMemAlloc : public IMemAlloc
{
public:
	void *Alloc( size_t nSize ) OVERRIDE
	{
		if ( !nSize )
			nSize = 1;
		return malloc( nSize );
	}
	void *Realloc( void *pMem, size_t nSize ) OVERRIDE
	{
		if ( !pMem )
			return Alloc( nSize );
		if ( !nSize )
		{
			free( pMem );
			return NULL;
		}
		return realloc( pMem, nSize );
	}
	void Free( void *pMem ) OVERRIDE
	{
		if ( pMem )
			free( pMem );
	}
	void *Expand_NoLongerSupported( void *pMem, size_t nSize ) OVERRIDE
	{
		(void)pMem; (void)nSize;
		return NULL;
	}

	void *Alloc( size_t nSize, const char *pFileName, int nLine ) OVERRIDE
	{
		(void)pFileName; (void)nLine;
		return Alloc( nSize );
	}
	void *Realloc( void *pMem, size_t nSize, const char *pFileName, int nLine ) OVERRIDE
	{
		(void)pFileName; (void)nLine;
		return Realloc( pMem, nSize );
	}
	void Free( void *pMem, const char *pFileName, int nLine ) OVERRIDE
	{
		(void)pFileName; (void)nLine;
		Free( pMem );
	}
	void *Expand_NoLongerSupported( void *pMem, size_t nSize, const char *pFileName, int nLine ) OVERRIDE
	{
		(void)pMem; (void)nSize; (void)pFileName; (void)nLine;
		return NULL;
	}

	size_t GetSize( void *pMem ) OVERRIDE
	{
		return pMem ? VGUI2_MSIZE( pMem ) : 0;
	}

	void PushAllocDbgInfo( const char *pFileName, int nLine ) OVERRIDE
	{
		(void)pFileName; (void)nLine;
	}
	void PopAllocDbgInfo() OVERRIDE {}

	long CrtSetBreakAlloc( long lNewBreakAlloc ) OVERRIDE
	{
		(void)lNewBreakAlloc;
		return -1;
	}
	int CrtSetReportMode( int nReportType, int nReportMode ) OVERRIDE
	{
		(void)nReportType; (void)nReportMode;
		return 0;
	}
	int CrtIsValidHeapPointer( const void *pMem ) OVERRIDE
	{
		return pMem ? 1 : 0;
	}
	int CrtIsValidPointer( const void *pMem, unsigned int size, int access ) OVERRIDE
	{
		(void)pMem; (void)size; (void)access;
		return 1;
	}
	int CrtCheckMemory( void ) OVERRIDE { return 1; }
	int CrtSetDbgFlag( int nNewFlag ) OVERRIDE
	{
		(void)nNewFlag;
		return 0;
	}
	void CrtMemCheckpoint( _CrtMemState *pState ) OVERRIDE
	{
		(void)pState;
	}

	void DumpStats() OVERRIDE {}

	void *CrtSetReportFile( int nRptType, void *hFile ) OVERRIDE
	{
		(void)nRptType;
		return hFile;
	}
	void *CrtSetReportHook( void *pfnNewHook ) OVERRIDE
	{
		(void)pfnNewHook;
		return NULL;
	}
	int CrtDbgReport( int nRptType, const char *szFile, int nLine, const char *szModule, const char *pMsg ) OVERRIDE
	{
		(void)nRptType; (void)szFile; (void)nLine; (void)szModule; (void)pMsg;
		return 0;
	}

	int heapchk() OVERRIDE { return 0; }

	bool IsDebugHeap() OVERRIDE { return false; }

	void GetActualDbgInfo( const char *&pFileName, int &nLine ) OVERRIDE
	{
		pFileName = "vgui2_base.cpp";
		nLine = 0;
	}
	void RegisterAllocation( const char *pFileName, int nLine, int nLogicalSize, int nActualSize, unsigned nTime ) OVERRIDE
	{
		(void)pFileName; (void)nLine; (void)nLogicalSize; (void)nActualSize; (void)nTime;
	}
	void RegisterDeallocation( const char *pFileName, int nLine, int nLogicalSize, int nActualSize, unsigned nTime ) OVERRIDE
	{
		(void)pFileName; (void)nLine; (void)nLogicalSize; (void)nActualSize; (void)nTime;
	}

	int GetVersion() OVERRIDE { return MEMALLOC_VERSION; }

	void CompactHeap() OVERRIDE {}

	MemAllocFailHandler_t SetAllocFailHandler( MemAllocFailHandler_t pfnMemAllocFailHandler ) OVERRIDE
	{
		MemAllocFailHandler_t prev = m_failHandler;
		m_failHandler = pfnMemAllocFailHandler;
		return prev;
	}

private:
	MemAllocFailHandler_t m_failHandler = nullptr;
};

static CBaseMemAlloc s_BaseMemAlloc;

IMemAlloc *g_pMemAlloc = &s_BaseMemAlloc;

#endif // !defined( NO_MALLOC_OVERRIDE )
