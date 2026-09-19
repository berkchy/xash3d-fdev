// vgui2_localize.cpp - ILocalize implementation for the VGUI2 GoldSource port.

#include <cstdio>
#include <cstring>
#include <cstdarg>

#include <vgui/ILocalize.h>
#include <KeyValues.h>
#include <tier1/utlvector.h>
#include <tier1/utlstring.h>
#include <tier1/utlbuffer.h>
#include <tier1/strtools.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

class IFileSystem;

namespace vgui2
{

struct LocalizeEntry
{
	char name[128];
	wchar_t *value;
	char file[128];
};

class CLocalizeImpl : public ILocalize
{
public:
	CLocalizeImpl() {}
	~CLocalizeImpl() { RemoveAll(); }

	bool AddFile( IFileSystem *fileSystem, const char *fileName ) OVERRIDE
	{
		(void)fileSystem;
		if ( !fileName || !fileName[0] )
			return false;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->loadFile )
			return false;
		unsigned char *buf = NULL;
		int len = b->loadFile( fileName, &buf );
		if ( len <= 0 || !buf )
			return false;

		KeyValues *kv = new KeyValues( "lang" );
		bool ok = kv->LoadFromBuffer( fileName, (const char *)buf, NULL, NULL );
		b->freeFile( buf );
		if ( !ok )
		{
			kv->deleteThis();
			return false;
		}

		// expected shape: "lang" { "Tokens" { "name" "value" ... } }
		KeyValues *tokens = kv->FindKey( "Tokens", false );
		if ( !tokens )
			tokens = kv; // tolerate flat files
		for ( KeyValues *k = tokens->GetFirstSubKey(); k; k = k->GetNextKey() )
		{
			const char *key = k->GetName();
			const char *val = k->GetString();
			if ( key && key[0] && val )
				AddToken( key, val, fileName );
		}
		if ( m_files.Find( fileName ) == m_files.InvalidIndex() )
			m_files.AddToTail( fileName );

		kv->deleteThis();
		return true;
	}

	void RemoveAll() OVERRIDE
	{
		for ( int i = 0; i < m_entries.Count(); i++ )
			delete[] m_entries[i].value;
		m_entries.Purge();
		m_files.Purge();
	}

	wchar_t *Find( char const *tokenName ) OVERRIDE
	{
		StringIndex_t idx = FindIndex( tokenName );
		if ( idx == INVALID_STRING_INDEX )
			return NULL;
		return m_entries[idx].value;
	}

	int ConvertANSIToUnicode( const char *ansi, wchar_t *unicode, int unicodeBufferSizeInBytes ) OVERRIDE
	{
		if ( !unicode || unicodeBufferSizeInBytes <= 0 )
			return 0;
		int max = unicodeBufferSizeInBytes / (int)sizeof( wchar_t );
		int i = 0;
		if ( ansi )
		{
			// UTF-8 decode with Latin-1 fallback
			const unsigned char *s = (const unsigned char *)ansi;
			while ( *s && i + 1 < max )
			{
				wchar_t ch;
				if ( *s < 0x80 )
				{
					ch = *s++;
				}
				else if (( *s & 0xE0 ) == 0xC0 && s[1] )
				{
					ch = (wchar_t)((( *s & 0x1F ) << 6 ) | ( s[1] & 0x3F ));
					s += 2;
				}
				else if (( *s & 0xF0 ) == 0xE0 && s[1] && s[2] )
				{
					ch = (wchar_t)((( *s & 0x0F ) << 12 ) | (( s[1] & 0x3F ) << 6 ) | ( s[2] & 0x3F ));
					s += 3;
				}
				else
				{
					ch = *s++;
				}
				unicode[i++] = ch;
			}
		}
		unicode[i] = 0;
		return i + 1;
	}

	int ConvertUnicodeToANSI( const wchar_t *unicode, char *ansi, int ansiBufferSize ) OVERRIDE
	{
		if ( !ansi || ansiBufferSize <= 0 )
			return 0;
		int i = 0;
		if ( unicode )
		{
			while ( *unicode && i + 1 < ansiBufferSize )
			{
				wchar_t ch = *unicode++;
				if ( ch < 0x80 )
				{
					ansi[i++] = (char)ch;
				}
				else if ( ch < 0x800 && i + 2 < ansiBufferSize )
				{
					ansi[i++] = (char)( 0xC0 | ( ch >> 6 ));
					ansi[i++] = (char)( 0x80 | ( ch & 0x3F ));
				}
				else if ( i + 3 < ansiBufferSize )
				{
					ansi[i++] = (char)( 0xE0 | ( ch >> 12 ));
					ansi[i++] = (char)( 0x80 | (( ch >> 6 ) & 0x3F ));
					ansi[i++] = (char)( 0x80 | ( ch & 0x3F ));
				}
				else break;
			}
		}
		ansi[i] = 0;
		return i + 1;
	}

	StringIndex_t FindIndex( const char *tokenName ) OVERRIDE
	{
		if ( !tokenName )
			return INVALID_STRING_INDEX;
		// '#' prefix is optional
		const char *name = ( tokenName[0] == '#' ) ? tokenName + 1 : tokenName;
		for ( int i = 0; i < m_entries.Count(); i++ )
		{
			if ( !Q_stricmp( m_entries[i].name, name ))
				return (StringIndex_t)i;
		}
		return INVALID_STRING_INDEX;
	}

	void ConstructString( wchar_t *unicodeOutput, int unicodeBufferSizeInBytes, wchar_t *formatString, int numFormatParameters, ... ) OVERRIDE
	{
		if ( !unicodeOutput || unicodeBufferSizeInBytes <= 0 )
			return;
		unicodeOutput[0] = 0;
		if ( !formatString )
			return;
		va_list args;
		va_start( args, numFormatParameters );
		const wchar_t *params[9] = { 0 };
		for ( int i = 0; i < numFormatParameters && i < 9; i++ )
			params[i] = va_arg( args, const wchar_t * );
		va_end( args );

		int max = unicodeBufferSizeInBytes / (int)sizeof( wchar_t );
		int o = 0;
		for ( const wchar_t *s = formatString; *s && o + 1 < max; s++ )
		{
			if ( *s == '%' && s[1] == 's' && s[2] >= '1' && s[2] <= '9' )
			{
				int idx = s[2] - '1';
				s += 2;
				const wchar_t *p = ( idx < 9 ) ? params[idx] : NULL;
				if ( p )
				{
					while ( *p && o + 1 < max )
						unicodeOutput[o++] = *p++;
				}
			}
			else unicodeOutput[o++] = *s;
		}
		unicodeOutput[o] = 0;
	}

	const char *GetNameByIndex( StringIndex_t index ) OVERRIDE
	{
		if ( index == INVALID_STRING_INDEX || index >= (StringIndex_t)m_entries.Count() )
			return NULL;
		return m_entries[index].name;
	}
	wchar_t *GetValueByIndex( StringIndex_t index ) OVERRIDE
	{
		if ( index == INVALID_STRING_INDEX || index >= (StringIndex_t)m_entries.Count() )
			return NULL;
		return m_entries[index].value;
	}

	StringIndex_t GetFirstStringIndex() OVERRIDE
	{
		return m_entries.Count() > 0 ? 0 : INVALID_STRING_INDEX;
	}
	StringIndex_t GetNextStringIndex( StringIndex_t index ) OVERRIDE
	{
		StringIndex_t next = index + 1;
		return next < (StringIndex_t)m_entries.Count() ? next : INVALID_STRING_INDEX;
	}

	void AddString( const char *tokenName, wchar_t *unicodeString, const char *fileName ) OVERRIDE
	{
		if ( !tokenName || !tokenName[0] )
			return;
		StringIndex_t idx = FindIndex( tokenName );
		if ( idx != INVALID_STRING_INDEX )
		{
			SetValueByIndex( idx, unicodeString );
			return;
		}
		LocalizeEntry e;
		Q_strncpy( e.name, ( tokenName[0] == '#' ) ? tokenName + 1 : tokenName, sizeof( e.name ));
		e.value = CopyWString( unicodeString );
		Q_strncpy( e.file, fileName ? fileName : "", sizeof( e.file ));
		m_entries.AddToTail( e );
	}

	void SetValueByIndex( StringIndex_t index, wchar_t *newValue ) OVERRIDE
	{
		if ( index == INVALID_STRING_INDEX || index >= (StringIndex_t)m_entries.Count() )
			return;
		delete[] m_entries[index].value;
		m_entries[index].value = CopyWString( newValue );
	}

	bool SaveToFile( IFileSystem *fileSystem, const char *fileName ) OVERRIDE
	{
		(void)fileSystem; (void)fileName;
		return false; // no write access through the backend
	}

	int GetLocalizationFileCount() OVERRIDE { return m_files.Count(); }
	const char *GetLocalizationFileName( int index ) OVERRIDE
	{
		if ( index < 0 || index >= m_files.Count() )
			return NULL;
		return m_files[index];
	}

	const char *GetFileNameByIndex( StringIndex_t index ) OVERRIDE
	{
		if ( index == INVALID_STRING_INDEX || index >= (StringIndex_t)m_entries.Count() )
			return NULL;
		return m_entries[index].file;
	}

	void ReloadLocalizationFiles( IFileSystem *filesystem ) OVERRIDE
	{
		(void)filesystem;
		// re-add known files
		for ( int i = 0; i < m_files.Count(); i++ )
		{
			const char *f = m_files[i];
			char tmp[256];
			Q_strncpy( tmp, f, sizeof( tmp ));
			RemoveAll();
			AddFile( NULL, tmp );
		}
	}

	void ConstructString( wchar_t *unicodeOutput, int unicodeBufferSizeInBytes, const char *tokenName, KeyValues *localizationVariables ) OVERRIDE
	{
		wchar_t *fmt = Find( tokenName );
		if ( !fmt )
		{
			if ( unicodeOutput && unicodeBufferSizeInBytes > 0 )
				unicodeOutput[0] = 0;
			return;
		}
		// gather %sN values from KeyValues ("s1".."s9")
		const wchar_t *params[9] = { 0 };
		wchar_t buffers[9][256];
		for ( int i = 0; i < 9; i++ )
		{
			char key[4];
			Q_snprintf( key, sizeof( key ), "s%d", i + 1 );
			const char *v = localizationVariables ? localizationVariables->GetString( key, NULL ) : NULL;
			if ( v )
			{
				ConvertANSIToUnicode( v, buffers[i], sizeof( buffers[i] ));
				params[i] = buffers[i];
			}
		}
		Substitute( unicodeOutput, unicodeBufferSizeInBytes, fmt, params );
	}
	void ConstructString( wchar_t *unicodeOutput, int unicodeBufferSizeInBytes, StringIndex_t unlocalizedTextSymbol, KeyValues *localizationVariables ) OVERRIDE
	{
		const char *name = GetNameByIndex( unlocalizedTextSymbol );
		ConstructString( unicodeOutput, unicodeBufferSizeInBytes, name ? name : "", localizationVariables );
	}

private:
	void AddToken( const char *key, const char *val, const char *fileName )
	{
		wchar_t wval[1024];
		ConvertANSIToUnicode( val, wval, sizeof( wval ));
		AddString( key, wval, fileName );
	}
	static wchar_t *CopyWString( const wchar_t *src )
	{
		if ( !src )
			src = L"";
		size_t n = 0;
		while ( src[n] ) n++;
		wchar_t *dst = new wchar_t[n + 1];
		for ( size_t i = 0; i <= n; i++ )
			dst[i] = src[i];
		return dst;
	}
	static void Substitute( wchar_t *out, int outBytes, const wchar_t *fmt, const wchar_t **params )
	{
		if ( !out || outBytes <= 0 )
			return;
		out[0] = 0;
		if ( !fmt )
			return;
		int max = outBytes / (int)sizeof( wchar_t );
		int o = 0;
		for ( const wchar_t *s = fmt; *s && o + 1 < max; s++ )
		{
			if ( *s == '%' && s[1] == 's' && s[2] >= '1' && s[2] <= '9' )
			{
				int idx = s[2] - '1';
				s += 2;
				const wchar_t *p = params[idx];
				if ( p )
				{
					while ( *p && o + 1 < max )
						out[o++] = *p++;
				}
			}
			else out[o++] = *s;
		}
		out[o] = 0;
	}

	CUtlVector<LocalizeEntry> m_entries;
	CUtlVector<CUtlString> m_files;
};

static CLocalizeImpl s_LocalizeImpl;

ILocalize *VGui2_GetLocalizeInterface() { return &s_LocalizeImpl; }

} // namespace vgui2
