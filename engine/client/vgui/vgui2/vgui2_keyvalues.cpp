// vgui2_keyvalues.cpp - KeyValues003 (IKeyValues) implementation.
//
// KeyValues objects allocate through this system interface, which in
// GoldSource is provided by the engine. Exposed via VGui2_CreateInterface.

#include <cstdlib>
#include <cstring>

#include <tier1/interface.h>
#include <tier1/utlvector.h>
#include <tier1/utlstring.h>
#include <tier1/strtools.h>
#include <vgui/ILocalize.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

// The KeyValues system binding lives in the tier2 sources; its header
// (3rdparty/vgui2/src/tier2/KeyValuesCompat.h) is not on the engine's include
// path, so the single entry point needed here is declared manually.
bool KV_InitKeyValuesSystem( CreateInterfaceFn *pFactories, int iNumFactories );

namespace vgui2
{

typedef int HKeySymbol;
#define INVALID_KEY_SYMBOL (-1)

class IKeyValues : public IBaseInterface
{
public:
	virtual void RegisterSizeofKeyValues( int size ) = 0;
	virtual void *AllocKeyValuesMemory( int size ) = 0;
	virtual void FreeKeyValuesMemory( void *pMem ) = 0;
	virtual HKeySymbol GetSymbolForString( const char *name ) = 0;
	virtual const char *GetStringForSymbol( HKeySymbol symbol ) = 0;
	virtual void GetLocalizedFromANSI( const char *ansi, wchar_t *outBuf, int unicodeBufferSizeInBytes ) = 0;
	virtual void GetANSIFromLocalized( const wchar_t *wchar, char *outBuf, int ansiBufferSizeInBytes ) = 0;
	virtual void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) = 0;
	virtual void RemoveKeyValuesFromMemoryLeakList( void *pMem ) = 0;
};

#define IKEYVALUES_INTERFACE_VERSION "KeyValues003"

class CKeyValuesSystem : public IKeyValues
{
public:
	CKeyValuesSystem() : m_size( 0 )
	{
		m_symbols.AddToTail( "" );
	}

	void RegisterSizeofKeyValues( int size ) OVERRIDE
	{
		if ( size > m_size )
			m_size = size;
	}
	void *AllocKeyValuesMemory( int size ) OVERRIDE
	{
		if ( size <= 0 )
			size = m_size;
		if ( size <= 0 )
			size = 64;
		return malloc((size_t)size );
	}
	void FreeKeyValuesMemory( void *pMem ) OVERRIDE
	{
		free( pMem );
	}
	HKeySymbol GetSymbolForString( const char *name ) OVERRIDE
	{
		if ( !name )
			return INVALID_KEY_SYMBOL;
		for ( int i = 0; i < m_symbols.Count(); i++ )
		{
			if ( !Q_stricmp( m_symbols[i], name ))
				return (HKeySymbol)i;
		}
		m_symbols.AddToTail( name );
		return (HKeySymbol)( m_symbols.Count() - 1 );
	}
	const char *GetStringForSymbol( HKeySymbol symbol ) OVERRIDE
	{
		if ( symbol < 0 || symbol >= m_symbols.Count() )
			return "";
		return m_symbols[symbol];
	}
	void GetLocalizedFromANSI( const char *ansi, wchar_t *outBuf, int unicodeBufferSizeInBytes ) OVERRIDE
	{
		ILocalize *loc = VGui2_GetLocalizeInterface();
		if ( loc )
			loc->ConvertANSIToUnicode( ansi ? ansi : "", outBuf, unicodeBufferSizeInBytes );
		else if ( outBuf && unicodeBufferSizeInBytes > 0 )
			outBuf[0] = 0;
	}
	void GetANSIFromLocalized( const wchar_t *wchar, char *outBuf, int ansiBufferSizeInBytes ) OVERRIDE
	{
		ILocalize *loc = VGui2_GetLocalizeInterface();
		if ( loc )
			loc->ConvertUnicodeToANSI( wchar, outBuf, ansiBufferSizeInBytes );
		else if ( outBuf && ansiBufferSizeInBytes > 0 )
			outBuf[0] = 0;
	}
	void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) OVERRIDE
	{
		(void)name;
		if ( pMem )
			m_leaks.AddToTail( pMem );
	}
	void RemoveKeyValuesFromMemoryLeakList( void *pMem ) OVERRIDE
	{
		m_leaks.FindAndRemove( pMem );
	}

private:
	int m_size;
	CUtlVector<CUtlString> m_symbols;
	CUtlVector<void *> m_leaks;
};

static CKeyValuesSystem s_KeyValuesSystem;

const char *VGui2_KeyValuesVersion() { return IKEYVALUES_INTERFACE_VERSION; }
IBaseInterface *VGui2_GetKeyValuesInterface() { return &s_KeyValuesSystem; }

//-----------------------------------------------------------------------------
// Binds the tier2 KeyValues wrapper (KeyValuesCompat.cpp) to this
// engine-provided implementation.
//
// KeyValues never allocates on its own: KeyValues::operator new and the key
// name symbol table both go through keyvalues(), which only forwards to an
// IKeyValues ("KeyValues003") instance once a factory handed one over. Game UI
// modules do that from ConnectTier2Libraries(), but the engine-side VGUI2 host
// runs at CL_InitLocal - before any client module is loaded - so the engine has
// to bind its own implementation first. Without that the first KeyValues
// allocation (parsing the scheme) calls through a NULL pointer and the process
// dies with SIGSEGV at 0x0 inside AllocKeyValuesMemory.
//-----------------------------------------------------------------------------
static void *VGui2_KeyValuesFactory( const char *name, int *returnCode )
{
	if ( returnCode )
		*returnCode = IFACE_FAILED;

	if ( !name || strcmp( name, IKEYVALUES_INTERFACE_VERSION ))
		return NULL;

	if ( returnCode )
		*returnCode = IFACE_OK;

	return &s_KeyValuesSystem;
}

void VGui2_BindKeyValuesSystem()
{
	// Idempotent: the wrapper keeps the first implementation it was given, so
	// calling this more than once (or after a game module bound one) is
	// harmless.
	CreateInterfaceFn factories[1] = { VGui2_KeyValuesFactory };

	KV_InitKeyValuesSystem( factories, 1 );
}

} // namespace vgui2
