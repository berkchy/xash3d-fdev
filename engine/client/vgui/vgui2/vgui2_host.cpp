// vgui2_host.cpp - engine-side VGUI2 host: embedded panel, demo dialog,
// input routing. Compiled into the engine (objects group).
//
// The host owns the IVGui embedded panel and pumps frames/paint/input for
// it. Game UI will use the same entry points; the demo dialog (cvar
// vgui2_demo) proves the whole stack works end to end.

#include <cstring>

// Valve tier0 base first: DLL_EXPORT, int64/uint64, OVERRIDE.
#include <tier0/platform.h>

// Standalone engine key numbers (no dependencies).
#include "keydefs.h"

// Engine runtime imports. Declared manually on purpose: engine's own
// headers are not C++-clean and collide with Valve headers (e.g. both
// define the PLATFORM_H include guard), so they are never included here.
// The struct only mirrors the first fields of engine's convar_s (the
// public prefix is ABI-frozen); only ->value is read.
extern "C"
{
void Con_Printf( const char *fmt, ... );
typedef struct host_convar_s
{
	char *name;
	char *string;
	unsigned int flags;
	float value;
	struct host_convar_s *next;
} host_convar_t;
host_convar_t *Cvar_Get( const char *name, const char *value, unsigned int flags, const char *desc );
}

#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/IInputInternal.h>
#include <vgui/ILocalize.h>
#include <vgui/ISystem.h>
#include <vgui/KeyCode.h>
#include <vgui/MouseCode.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Controls.h>

// Tier connect: fills the vgui_controls globals (g_pVGui, g_pVGuiPanel,
// g_pVGuiSurface, ...) from our factory. Must run before any Valve control
// is constructed.
#include <tier1/interface.h>
#include <tier2/tier2.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

//-----------------------------------------------------------------------------
// Embedded demo scheme: Default + Title (outlined = thick) fonts, basic colors.
//-----------------------------------------------------------------------------
static const char s_demoScheme[] =
"Scheme\n"
"{\n"
"	Colors\n"
"	{\n"
"		\"FgColor\" \"200 200 200 255\"\n"
"		\"BgColor\" \"60 60 60 255\"\n"
"		\"Label.TextColor\" \"255 255 255 255\"\n"
"		\"Label.BgColor\" \"60 60 60 255\"\n"
"		\"Button.TextColor\" \"255 255 255 255\"\n"
"		\"Button.BgColor\" \"80 80 80 255\"\n"
"		\"Frame.BgColor\" \"55 55 65 255\"\n"
"		\"Frame.OutOfFocusBgColor\" \"45 45 55 255\"\n"
"		\"Frame.TitleTextColor\" \"255 220 120 255\"\n"
"	}\n"
"	BaseSettings\n"
"	{\n"
"		\"FgColor\" \"200 200 200 255\"\n"
"		\"BgColor\" \"60 60 60 255\"\n"
"	}\n"
"	Fonts\n"
"	{\n"
"		\"Default\"\n"
"		{\n"
"			\"1\"\n"
"			{\n"
"				\"name\" \"Arial\"\n"
"				\"tall\" \"15\"\n"
"				\"weight\" \"400\"\n"
"				\"antialias\" \"1\"\n"
"			}\n"
"		}\n"
"		\"Title\"\n"
"		{\n"
"			\"1\"\n"
"			{\n"
"				\"name\" \"Arial\"\n"
"				\"tall\" \"24\"\n"
"				\"weight\" \"700\"\n"
"				\"antialias\" \"1\"\n"
"				\"outline\" \"1\"\n"
"			}\n"
"		}\n"
"	}\n"
"	Borders\n"
"	{\n"
"	}\n"
"}\n";

//-----------------------------------------------------------------------------
// Demo dialog: thick title label + close button.
//-----------------------------------------------------------------------------
class CDemoDialog : public vgui2::Frame
{
	typedef vgui2::Frame BaseClass;
public:
	CDemoDialog() : vgui2::Frame( NULL, "VGui2Demo", true )
	{
		SetTitle( "VGUI2", false );
		SetSize( 420, 200 );
		SetMoveable( true );
		SetSizeable( false );
		SetMenuButtonResponsive( false );
		SetMinimizeButtonVisible( false );
		SetMaximizeButtonVisible( false );

		m_pTitle = new vgui2::Label( this, "DemoTitle", "VGUI2 calisiyor!" );
		m_pTitle->SetPos( 20, 40 );
		m_pTitle->SetSize( 380, 40 );

		m_pInfo = new vgui2::Label( this, "DemoInfo", "ISurface + IVGui + IPanel + Input" );
		m_pInfo->SetPos( 20, 84 );
		m_pInfo->SetSize( 380, 24 );

		m_pClose = new vgui2::Button( this, "DemoClose", "Kapat", this, "Close" );
		m_pClose->SetPos( 160, 140 );
		m_pClose->SetSize( 100, 28 );
	}

	void ApplySchemeSettings( vgui2::IScheme *pScheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( pScheme );
		vgui2::HFont titleFont = pScheme->GetFont( "Title", IsProportional() );
		if ( titleFont != vgui2::INVALID_FONT )
			m_pTitle->SetFont( titleFont );
	}

private:
	vgui2::Label *m_pTitle;
	vgui2::Label *m_pInfo;
	vgui2::Button *m_pClose;
};

//-----------------------------------------------------------------------------
// Engine keynum (keydefs.h) -> vgui2 KeyCode translation.
// VGUI1 codes are NOT reused: VGUI1 KEY_0=0, VGUI2 KEY_0=1.
//-----------------------------------------------------------------------------
static vgui2::KeyCode EngineKeyToVGui2( int key )
{
	using namespace vgui2;
	// printable ASCII first
	if ( key >= '0' && key <= '9' )
		return (KeyCode)( KEY_0 + ( key - '0' ));
	if ( key >= 'a' && key <= 'z' )
		return (KeyCode)( KEY_A + ( key - 'a' ));
	if ( key >= 'A' && key <= 'Z' )
		return (KeyCode)( KEY_A + ( key - 'A' ));
	switch ( key )
	{
	case ' ': return KEY_SPACE;
	case ',': return KEY_COMMA;
	case '.': return KEY_PERIOD;
	case '/': return KEY_SLASH;
	case ';': return KEY_SEMICOLON;
	case '\'': return KEY_APOSTROPHE;
	case '[': return KEY_LBRACKET;
	case ']': return KEY_RBRACKET;
	case '-': return KEY_MINUS;
	case '=': return KEY_EQUAL;
	case '\\': return KEY_BACKSLASH;
	case '`': return KEY_BACKQUOTE;
	case K_TAB: return KEY_TAB;
	case K_ENTER: return KEY_ENTER;
	case K_ESCAPE: return KEY_ESCAPE;
	case K_BACKSPACE: return KEY_BACKSPACE;
	case K_UPARROW: return KEY_UP;
	case K_DOWNARROW: return KEY_DOWN;
	case K_LEFTARROW: return KEY_LEFT;
	case K_RIGHTARROW: return KEY_RIGHT;
	case K_ALT: return KEY_LALT;
	case K_CTRL: return KEY_LCONTROL;
	case K_SHIFT: return KEY_LSHIFT;
	case K_F1: return KEY_F1;
	case K_F2: return KEY_F2;
	case K_F3: return KEY_F3;
	case K_F4: return KEY_F4;
	case K_F5: return KEY_F5;
	case K_F6: return KEY_F6;
	case K_F7: return KEY_F7;
	case K_F8: return KEY_F8;
	case K_F9: return KEY_F9;
	case K_F10: return KEY_F10;
	case K_F11: return KEY_F11;
	case K_F12: return KEY_F12;
	case K_INS: return KEY_INSERT;
	case K_DEL: return KEY_DELETE;
	case K_PGDN: return KEY_PAGEDOWN;
	case K_PGUP: return KEY_PAGEUP;
	case K_HOME: return KEY_HOME;
	case K_END: return KEY_END;
	case K_KP_HOME: return KEY_PAD_7;
	case K_KP_UPARROW: return KEY_PAD_8;
	case K_KP_PGUP: return KEY_PAD_9;
	case K_KP_LEFTARROW: return KEY_PAD_4;
	case K_KP_5: return KEY_PAD_5;
	case K_KP_RIGHTARROW: return KEY_PAD_6;
	case K_KP_END: return KEY_PAD_1;
	case K_KP_DOWNARROW: return KEY_PAD_2;
	case K_KP_PGDN: return KEY_PAD_3;
	case K_KP_ENTER: return KEY_PAD_ENTER;
	case K_KP_INS: return KEY_PAD_0;
	case K_KP_DEL: return KEY_PAD_DECIMAL;
	case K_KP_SLASH: return KEY_PAD_DIVIDE;
	case K_KP_MINUS: return KEY_PAD_MINUS;
	case K_KP_PLUS: return KEY_PAD_PLUS;
	case K_KP_MUL: return KEY_PAD_MULTIPLY;
	case K_CAPSLOCK: return KEY_CAPSLOCK;
	case K_WIN: return KEY_LWIN;
	case K_KP_NUMLOCK: return KEY_NUMLOCK;
	case K_PAUSE: return KEY_BREAK;
	default: return KEY_NONE;
	}
}

//-----------------------------------------------------------------------------
// Host state.
//-----------------------------------------------------------------------------
static struct
{
	bool inited;
	vgui2::VPANEL embedded;
	vgui2::HScheme scheme;
	CDemoDialog *dialog;
	host_convar_t *demoCvar;
	int mouseX, mouseY;
} s_host;

static bool HostDemoVisible()
{
	return s_host.inited && s_host.demoCvar && s_host.demoCvar->value != 0.0f;
}

//-----------------------------------------------------------------------------
// Entry points called from vgui_draw.c (C linkage).
//-----------------------------------------------------------------------------
extern "C" void VGui2_HostInit( void )
{
	if ( s_host.inited )
		return;

	// KeyValues (scheme parsing, vgui2 controls) allocates through the tier2
	// wrapper, which has to be bound to the engine's KeyValues003 before the
	// first allocation: nothing else runs this early in the engine, so without
	// this the scheme load below would call through a NULL interface.
	vgui2::VGui2_BindKeyValuesSystem();

	VGui2_InitEngineBackend();

	// Publish the engine implementations into the vgui_controls globals
	// (ivgui(), ipanel(), surface(), scheme(), ...). Valve Panel/Frame/
	// Label/Button constructors dereference these; without this step any
	// `new Frame` segfaults in Panel::Init (the vgui2_demo device crash).
	// Same pattern the game client uses in CSB_SetEngineFactory.
	CreateInterfaceFn engineFactory = vgui2::VGui2_CreateInterface;
	ConnectTier1Libraries( &engineFactory, 1 );
	ConnectTier2Libraries( &engineFactory, 1 );
	if ( !vgui2::VGui_InitInterfacesList( "Engine", &engineFactory, 1 ))
		Con_Printf( "VGUI2 host: VGui_InitInterfacesList failed\n" );

	vgui2::IVGui *vgui = vgui2::VGui2_GetVGuiInterface();
	vgui2::IPanel *panel = vgui2::VGui2_GetPanelInterface();
	vgui2::ISurface *surface = vgui2::VGui2_GetSurfaceInterface();

	vgui->Start();

	s_host.embedded = vgui->AllocPanel();
	panel->Init( s_host.embedded, NULL );

	int wide = 640, tall = 480;
	surface->GetScreenSize( wide, tall );
	panel->SetPos( s_host.embedded, 0, 0 );
	panel->SetSize( s_host.embedded, wide, tall );
	panel->SetVisible( s_host.embedded, true );
	surface->SetEmbeddedPanel( s_host.embedded );

	s_host.scheme = vgui2::VGui2_LoadSchemeFromBuffer( s_demoScheme, "DemoScheme" );

	s_host.demoCvar = Cvar_Get( "vgui2_demo", "0", 0, "Show the VGUI2 demo dialog" );
	s_host.mouseX = wide / 2;
	s_host.mouseY = tall / 2;
	s_host.inited = true;

	Con_Printf( "VGUI2 host initialized\n" );
}

extern "C" void VGui2_HostFrame( void )
{
	if ( !s_host.inited )
		return;

	vgui2::IVGui *vgui = vgui2::VGui2_GetVGuiInterface();
	vgui2::IPanel *panel = vgui2::VGui2_GetPanelInterface();
	vgui2::ISurface *surface = vgui2::VGui2_GetSurfaceInterface();
	vgui2::IInputInternal *input = vgui2::VGui2_GetInputInterface();

	// keep embedded panel at screen size
	int wide = 640, tall = 480;
	surface->GetScreenSize( wide, tall );
	panel->SetSize( s_host.embedded, wide, tall );

	// demo dialog lifecycle follows the cvar
	if ( HostDemoVisible() && !s_host.dialog )
	{
		s_host.dialog = new CDemoDialog();
		vgui2::VPANEL vp = s_host.dialog->GetVPanel();
		panel->SetParent( vp, s_host.embedded );
		s_host.dialog->MoveToCenterOfScreen();
		s_host.dialog->Activate();
		s_host.dialog->SetVisible( true );
	}
	if ( !HostDemoVisible() && s_host.dialog )
	{
		// ~Panel frees its VPANEL via IVGui::FreePanel
		delete s_host.dialog;
		s_host.dialog = NULL;
	}

	vgui->RunFrame();
	surface->PaintTraverse( s_host.embedded );
	input->RunFrame();
}

extern "C" void VGui2_HostShutdown( void )
{
	if ( !s_host.inited )
		return;

	vgui2::IVGui *vgui = vgui2::VGui2_GetVGuiInterface();
	if ( s_host.dialog )
	{
		delete s_host.dialog;
		s_host.dialog = NULL;
	}
	if ( s_host.embedded )
	{
		vgui->FreePanel( s_host.embedded );
		s_host.embedded = 0;
	}
	vgui->Stop();
	vgui->Shutdown();
	vgui2::VGui2_GetSurfaceInterface()->Shutdown();
	vgui2::VGui2_GetSchemeManagerInterface()->Shutdown( true );
	vgui2::VGui2_GetSystemInterface()->Shutdown();
	s_host.inited = false;
}

extern "C" void VGui2_HostKey( int key, int down )
{
	if ( !HostDemoVisible() )
		return;
	vgui2::IInputInternal *input = vgui2::VGui2_GetInputInterface();
	vgui2::KeyCode code = EngineKeyToVGui2( key );
	if ( code == vgui2::KEY_NONE )
		return;
	if ( down )
	{
		input->InternalKeyCodePressed( code );
		input->InternalKeyCodeTyped( code );
		if ( key >= 32 && key < 127 )
			input->InternalKeyTyped((wchar_t)key );
	}
	else input->InternalKeyCodeReleased( code );
}

extern "C" void VGui2_HostMouse( int engineButton, int clicks )
{
	if ( !HostDemoVisible() )
		return;
	vgui2::IInputInternal *input = vgui2::VGui2_GetInputInterface();
	vgui2::MouseCode code = vgui2::MOUSE_LEFT;
	if ( engineButton == K_MOUSE2 )
		code = vgui2::MOUSE_RIGHT;
	else if ( engineButton == K_MOUSE3 )
		code = vgui2::MOUSE_MIDDLE;
	input->InternalCursorMoved( s_host.mouseX, s_host.mouseY );
	if ( clicks >= 2 )
		input->InternalMouseDoublePressed( code );
	else if ( clicks == 1 )
		input->InternalMousePressed( code );
	else
		input->InternalMouseReleased( code );
}

extern "C" void VGui2_HostMouseMove( int x, int y )
{
	if ( !s_host.inited )
		return;
	s_host.mouseX = x;
	s_host.mouseY = y;
	if ( !HostDemoVisible() )
		return;
	vgui2::VGui2_GetInputInterface()->InternalCursorMoved( x, y );
}

extern "C" void VGui2_HostWheel( int delta )
{
	if ( !HostDemoVisible() )
		return;
	vgui2::VGui2_GetInputInterface()->InternalMouseWheeled( delta );
}
