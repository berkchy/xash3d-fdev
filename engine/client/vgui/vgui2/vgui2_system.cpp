// vgui2_system.cpp - ISystem implementation for the VGUI2 GoldSource port.

#include <cstdio>
#include <cstring>
#include <ctime>

#include <vgui/ISystem.h>
#include <vgui/KeyCode.h>
#include <KeyValues.h>
#include <tier1/utlvector.h>
#include <tier1/strtools.h>
#include <tier0/icommandline.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

// Provided by the static vgui_controls layer (vgui_key_translation.cpp).
vgui2::KeyCode KeyCode_VirtualKeyToVGUI( int key );
int KeyCode_VGUIToVirtualKey( vgui2::KeyCode keycode );

namespace vgui2
{

struct RegEntry
{
	char key[128];
	char strVal[256];
	int intVal;
	bool isInt;
};

class CSystemImpl : public ISystem
{
public:
	CSystemImpl() : m_config( NULL )
	{
		m_configFile[0] = 0;
		m_configPath[0] = 0;
		m_startTime = Now();
		m_watchForUse = false;
	}

	void Shutdown() OVERRIDE
	{
		if ( m_config )
		{
			m_config->deleteThis();
			m_config = NULL;
		}
	}

	void RunFrame() OVERRIDE {}

	void ShellExecute( const char *command, const char *file ) OVERRIDE
	{
		(void)command; (void)file;
		Log( "ShellExecute: not supported on this platform\n" );
	}

	double GetFrameTime() OVERRIDE { return Now() - m_startTime; }
	double GetCurrentTime() OVERRIDE { return Now(); }
	long GetTimeMillis() OVERRIDE { return (long)( Now() * 1000.0 ); }

	int GetClipboardTextCount() OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->getClipboardText )
			return 0;
		char tmp[16];
		return b->getClipboardText( tmp, sizeof( tmp )) > 0 ? 1 : 0;
	}
	void SetClipboardText( const char *text, int textLen ) OVERRIDE
	{
		(void)textLen;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->setClipboardText )
			b->setClipboardText( text ? text : "" );
	}
	void SetClipboardText( const wchar_t *text, int textLen ) OVERRIDE
	{
		(void)textLen;
		char buf[1024];
		UnicodeToAnsi( text, buf, sizeof( buf ));
		SetClipboardText( buf, (int)strlen( buf ));
	}
	int GetClipboardText( int offset, char *buf, int bufLen ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->getClipboardText || !buf || bufLen <= 0 )
			return 0;
		char tmp[4096];
		int len = b->getClipboardText( tmp, sizeof( tmp ));
		if ( len <= offset )
			return 0;
		Q_strncpy( buf, tmp + offset, bufLen );
		return (int)strlen( buf );
	}
	int GetClipboardText( int offset, wchar_t *buf, int bufLen ) OVERRIDE
	{
		char tmp[4096];
		int n = GetClipboardText( offset, tmp, sizeof( tmp ));
		if ( buf && bufLen > 0 )
			AnsiToUnicode( tmp, buf, bufLen );
		return n;
	}

	bool SetRegistryString( const char *key, const char *value ) OVERRIDE
	{
		RegEntry *e = FindReg( key, true );
		if ( !e )
			return false;
		Q_strncpy( e->strVal, value ? value : "", sizeof( e->strVal ));
		e->isInt = false;
		return true;
	}
	bool GetRegistryString( const char *key, char *value, int valueLen ) OVERRIDE
	{
		RegEntry *e = FindReg( key, false );
		if ( !e || e->isInt || !value || valueLen <= 0 )
			return false;
		Q_strncpy( value, e->strVal, valueLen );
		return true;
	}
	bool SetRegistryInteger( const char *key, int value ) OVERRIDE
	{
		RegEntry *e = FindReg( key, true );
		if ( !e )
			return false;
		e->intVal = value;
		e->isInt = true;
		return true;
	}
	bool GetRegistryInteger( const char *key, int &value ) OVERRIDE
	{
		RegEntry *e = FindReg( key, false );
		if ( !e || !e->isInt )
			return false;
		value = e->intVal;
		return true;
	}

	KeyValues *GetUserConfigFileData( const char *dialogName, int dialogID ) OVERRIDE
	{
		(void)dialogID;
		if ( !m_config )
			return NULL;
		return m_config->FindKey( dialogName, false );
	}
	void SetUserConfigFile( const char *fileName, const char *pathName ) OVERRIDE
	{
		Q_strncpy( m_configFile, fileName ? fileName : "", sizeof( m_configFile ));
		Q_strncpy( m_configPath, pathName ? pathName : "", sizeof( m_configPath ));
		LoadUserConfig();
	}
	void SaveUserConfigFile() OVERRIDE
	{
		// disk write is not exposed by the backend; keep in memory
		Log( "SaveUserConfigFile: persistence not supported, kept in memory\n" );
	}

	bool SetWatchForComputerUse( bool state ) OVERRIDE
	{
		m_watchForUse = state;
		return false;
	}
	double GetTimeSinceLastUse() OVERRIDE { return 0.0; }

	int GetAvailableDrives( char *buf, int bufLen ) OVERRIDE
	{
		if ( buf && bufLen > 2 )
		{
			buf[0] = 0;
			return 0;
		}
		return 0;
	}

	bool CommandLineParamExists( const char *paramName ) OVERRIDE
	{
		if ( !paramName )
			return false;
		char withDash[128];
		Q_snprintf( withDash, sizeof( withDash ), "-%s", paramName );
		return CommandLine()->FindParm( withDash ) != 0
			|| CommandLine()->FindParm( paramName ) != 0;
	}

	const char *GetFullCommandLine() OVERRIDE
	{
		return CommandLine()->GetCmdLine();
	}

	bool GetCurrentTimeAndDate( int *year, int *month, int *dayOfWeek, int *day, int *hour, int *minute, int *second ) OVERRIDE
	{
		time_t t = time( NULL );
		struct tm *lt = localtime( &t );
		if ( !lt )
			return false;
		if ( year ) *year = lt->tm_year + 1900;
		if ( month ) *month = lt->tm_mon + 1;
		if ( dayOfWeek ) *dayOfWeek = lt->tm_wday;
		if ( day ) *day = lt->tm_mday;
		if ( hour ) *hour = lt->tm_hour;
		if ( minute ) *minute = lt->tm_min;
		if ( second ) *second = lt->tm_sec;
		return true;
	}

	double GetFreeDiskSpace( const char *path ) OVERRIDE
	{
		(void)path;
		return 8.0 * 1024 * 1024 * 1024;
	}

	bool CreateShortcut( const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory, const char *iconFile ) OVERRIDE
	{
		(void)linkFileName; (void)targetPath; (void)arguments; (void)workingDirectory; (void)iconFile;
		return false;
	}
	bool GetShortcutTarget( const char *linkFileName, char *targetPath, char *arguments, int destBufferSizes ) OVERRIDE
	{
		(void)linkFileName; (void)targetPath; (void)arguments; (void)destBufferSizes;
		return false;
	}
	bool ModifyShortcutTarget( const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory ) OVERRIDE
	{
		(void)linkFileName; (void)targetPath; (void)arguments; (void)workingDirectory;
		return false;
	}

	bool GetCommandLineParamValue( const char *paramName, char *value, int valueBufferSize ) OVERRIDE
	{
		if ( !paramName || !value || valueBufferSize <= 0 )
			return false;
		const char *v = CommandLine()->ParmValue( paramName, (const char *)NULL );
		if ( !v )
		{
			char withDash[128];
			Q_snprintf( withDash, sizeof( withDash ), "-%s", paramName );
			v = CommandLine()->ParmValue( withDash, (const char *)NULL );
		}
		if ( !v )
			return false;
		Q_strncpy( value, v, valueBufferSize );
		return true;
	}

	bool DeleteRegistryKey( const char *keyName ) OVERRIDE
	{
		for ( int i = 0; i < m_reg.Count(); i++ )
		{
			if ( !strcmp( m_reg[i].key, keyName ? keyName : "" ))
			{
				m_reg.Remove( i );
				return true;
			}
		}
		return false;
	}

	const char *GetDesktopFolderPath() OVERRIDE { return "."; }

	KeyCode KeyCode_VirtualKeyToVGUI( int keyCode ) OVERRIDE
	{
		return ::KeyCode_VirtualKeyToVGUI( keyCode );
	}
	int KeyCode_VGUIToVirtualKey( KeyCode keyCode ) OVERRIDE
	{
		return ::KeyCode_VGUIToVirtualKey( keyCode );
	}

	const char *GetStartMenuFolderPath() OVERRIDE { return "."; }
	const char *GetAllUserDesktopFolderPath() OVERRIDE { return "."; }
	const char *GetAllUserStartMenuFolderPath() OVERRIDE { return "."; }

private:
	double Now()
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		return b && b->getTime ? b->getTime() : 0.0;
	}
	void Log( const char *msg )
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->log )
			b->log( msg );
	}
	RegEntry *FindReg( const char *key, bool create )
	{
		if ( !key )
			return NULL;
		for ( int i = 0; i < m_reg.Count(); i++ )
		{
			if ( !strcmp( m_reg[i].key, key ))
				return &m_reg[i];
		}
		if ( !create )
			return NULL;
		int idx = m_reg.AddToTail();
		RegEntry *e = &m_reg[idx];
		memset( e, 0, sizeof( *e ));
		Q_strncpy( e->key, key, sizeof( e->key ));
		return e;
	}
	void LoadUserConfig()
	{
		if ( m_config )
		{
			m_config->deleteThis();
			m_config = NULL;
		}
		if ( !m_configFile[0] )
			return;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->loadFile )
			return;
		unsigned char *buf = NULL;
		int len = b->loadFile( m_configFile, &buf );
		if ( len <= 0 || !buf )
			return;
		m_config = new KeyValues( "UserConfig" );
		m_config->LoadFromBuffer( m_configFile, (const char *)buf, NULL, NULL );
		b->freeFile( buf );
	}
	static void UnicodeToAnsi( const wchar_t *src, char *dst, int dstLen )
	{
		if ( !dst || dstLen <= 0 )
			return;
		int i = 0;
		if ( src )
		{
			while ( *src && i + 1 < dstLen )
				dst[i++] = (char)( *src++ & 0xFF );
		}
		dst[i] = 0;
	}
	static void AnsiToUnicode( const char *src, wchar_t *dst, int dstLenBytes )
	{
		if ( !dst || dstLenBytes <= 0 )
			return;
		int max = dstLenBytes / (int)sizeof( wchar_t );
		int i = 0;
		if ( src )
		{
			while ( *src && i + 1 < max )
				dst[i++] = (wchar_t)(unsigned char)*src++;
		}
		dst[i] = 0;
	}

	CUtlVector<RegEntry> m_reg;
	KeyValues *m_config;
	char m_configFile[256];
	char m_configPath[64];
	double m_startTime;
	bool m_watchForUse;
};

static CSystemImpl s_SystemImpl;

ISystem *VGui2_GetSystemInterface() { return &s_SystemImpl; }

} // namespace vgui2
