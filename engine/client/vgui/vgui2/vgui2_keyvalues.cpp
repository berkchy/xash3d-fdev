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

} // namespace vgui2
