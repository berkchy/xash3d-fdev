// vgui2_input.cpp - IInputInternal implementation for the VGUI2 GoldSource port.

#include <cstring>

#include <vgui/IInputInternal.h>
#include <vgui/IPanel.h>
#include <KeyValues.h>
#include <tier1/utlvector.h>
#include <tier1/strtools.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

namespace vgui2
{

#define VGUI2_MAX_KEYCODE 256
#define VGUI2_MAX_MOUSECODE 8

class CInputImpl : public IInputInternal
{
public:
	CInputImpl()
	{
		memset( m_mouseDown, 0, sizeof( m_mouseDown ));
		memset( m_keyDown, 0, sizeof( m_keyDown ));
		memset( m_mousePressed, 0, sizeof( m_mousePressed ));
		memset( m_mouseReleased, 0, sizeof( m_mouseReleased ));
		memset( m_mouseDouble, 0, sizeof( m_mouseDouble ));
		memset( m_keyPressed, 0, sizeof( m_keyPressed ));
		memset( m_keyTyped, 0, sizeof( m_keyTyped ));
		memset( m_keyReleased, 0, sizeof( m_keyReleased ));
		m_mouseFocus = NULL_HANDLE;
		m_keyFocus = NULL_HANDLE;
		m_mouseOver = NULL_HANDLE;
		m_mouseCapture = NULL_HANDLE;
		m_appModal = NULL_HANDLE;
		m_cursorOverride = NULL_HANDLE;
		m_cursorX = 0; m_cursorY = 0;
		m_context = 1; m_activeContext = 1;
	}

	//--- IInput ---
	void SetMouseFocus( VPANEL newMouseFocus ) OVERRIDE { m_mouseFocus = newMouseFocus; }
	void SetMouseCapture( VPANEL panel ) OVERRIDE { m_mouseCapture = panel; }

	void GetKeyCodeText( KeyCode code, char *buf, int buflen ) OVERRIDE
	{
		if ( !buf || buflen <= 0 )
			return;
		buf[0] = 0;
		const char *name = KeyCodeToName( code );
		if ( name )
			Q_strncpy( buf, name, buflen );
	}

	VPANEL GetFocus() OVERRIDE { return m_keyFocus; }
	VPANEL GetMouseOver() OVERRIDE { return m_mouseOver; }

	void SetCursorPos( int x, int y ) OVERRIDE
	{
		m_cursorX = x; m_cursorY = y;
	}
	void GetCursorPos( int &x, int &y ) OVERRIDE
	{
		x = m_cursorX; y = m_cursorY;
	}
	bool WasMousePressed( MouseCode code ) OVERRIDE
	{
		return ValidMouse( code ) ? m_mousePressed[code] : false;
	}
	bool WasMouseDoublePressed( MouseCode code ) OVERRIDE
	{
		return ValidMouse( code ) ? m_mouseDouble[code] : false;
	}
	bool IsMouseDown( MouseCode code ) OVERRIDE
	{
		return ValidMouse( code ) ? m_mouseDown[code] : false;
	}

	void SetCursorOveride( HCursor cursor ) OVERRIDE { m_cursorOverride = cursor; }
	HCursor GetCursorOveride() OVERRIDE { return m_cursorOverride; }

	bool WasMouseReleased( MouseCode code ) OVERRIDE
	{
		return ValidMouse( code ) ? m_mouseReleased[code] : false;
	}
	bool WasKeyPressed( KeyCode code ) OVERRIDE
	{
		return ValidKey( code ) ? m_keyPressed[code] : false;
	}
	bool IsKeyDown( KeyCode code ) OVERRIDE
	{
		return ValidKey( code ) ? m_keyDown[code] : false;
	}
	bool WasKeyTyped( KeyCode code ) OVERRIDE
	{
		return ValidKey( code ) ? m_keyTyped[code] : false;
	}
	bool WasKeyReleased( KeyCode code ) OVERRIDE
	{
		return ValidKey( code ) ? m_keyReleased[code] : false;
	}

	VPANEL GetAppModalSurface() OVERRIDE { return m_appModal; }
	void SetAppModalSurface( VPANEL panel ) OVERRIDE { m_appModal = panel; }
	void ReleaseAppModalSurface() OVERRIDE { m_appModal = NULL_HANDLE; }

	void GetCursorPosition( int &x, int &y ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->getMousePos )
			b->getMousePos( &x, &y );
		else { x = m_cursorX; y = m_cursorY; }
	}

	//--- IInputInternal ---
	void RunFrame() OVERRIDE
	{
		memset( m_mousePressed, 0, sizeof( m_mousePressed ));
		memset( m_mouseReleased, 0, sizeof( m_mouseReleased ));
		memset( m_mouseDouble, 0, sizeof( m_mouseDouble ));
		memset( m_keyPressed, 0, sizeof( m_keyPressed ));
		memset( m_keyTyped, 0, sizeof( m_keyTyped ));
		memset( m_keyReleased, 0, sizeof( m_keyReleased ));
	}

	void UpdateMouseFocus( int x, int y ) OVERRIDE
	{
		m_cursorX = x; m_cursorY = y;
		if ( m_mouseCapture )
		{
			m_mouseOver = m_mouseCapture;
			return;
		}
		m_mouseOver = FindPanelAt( x, y );
		if ( m_mouseOver != m_mouseFocus && !IsAnyMouseDown() )
		{
			// focus follows mouse when no button held
		}
	}

	void PanelDeleted( VPANEL panel ) OVERRIDE
	{
		if ( m_mouseFocus == panel ) m_mouseFocus = NULL_HANDLE;
		if ( m_keyFocus == panel ) m_keyFocus = NULL_HANDLE;
		if ( m_mouseOver == panel ) m_mouseOver = NULL_HANDLE;
		if ( m_mouseCapture == panel ) m_mouseCapture = NULL_HANDLE;
		if ( m_appModal == panel ) m_appModal = NULL_HANDLE;
	}

	void InternalCursorMoved( int x, int y ) OVERRIDE
	{
		UpdateMouseFocus( x, y );
	}
	void InternalMousePressed( MouseCode code ) OVERRIDE
	{
		if ( !ValidMouse( code ))
			return;
		m_mouseDown[code] = true;
		m_mousePressed[code] = true;
		VPANEL target = m_mouseCapture ? m_mouseCapture : FindPanelAt( m_cursorX, m_cursorY );
		m_mouseFocus = target;
		if ( target )
		{
			// mouse press also grabs keyboard focus
			m_keyFocus = target;
			KeyValues *kv = new KeyValues( "MousePressed" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalMouseDoublePressed( MouseCode code ) OVERRIDE
	{
		if ( !ValidMouse( code ))
			return;
		m_mouseDouble[code] = true;
		VPANEL target = m_mouseCapture ? m_mouseCapture : m_mouseFocus;
		if ( target )
		{
			KeyValues *kv = new KeyValues( "MouseDoublePressed" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalMouseReleased( MouseCode code ) OVERRIDE
	{
		if ( !ValidMouse( code ))
			return;
		m_mouseDown[code] = false;
		m_mouseReleased[code] = true;
		VPANEL target = m_mouseCapture ? m_mouseCapture : m_mouseFocus;
		if ( target )
		{
			KeyValues *kv = new KeyValues( "MouseReleased" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalMouseWheeled( int delta ) OVERRIDE
	{
		VPANEL target = m_mouseCapture ? m_mouseCapture : FindPanelAt( m_cursorX, m_cursorY );
		if ( target )
		{
			KeyValues *kv = new KeyValues( "MouseWheeled" );
			kv->SetInt( "delta", delta );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalKeyCodePressed( KeyCode code ) OVERRIDE
	{
		if ( !ValidKey( code ))
			return;
		m_keyDown[code] = true;
		m_keyPressed[code] = true;
		VPANEL target = PickKeyTarget();
		if ( target )
		{
			KeyValues *kv = new KeyValues( "KeyCodePressed" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalKeyCodeTyped( KeyCode code ) OVERRIDE
	{
		if ( !ValidKey( code ))
			return;
		m_keyTyped[code] = true;
		VPANEL target = PickKeyTarget();
		if ( target )
		{
			KeyValues *kv = new KeyValues( "KeyCodeTyped" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalKeyTyped( wchar_t unichar ) OVERRIDE
	{
		VPANEL target = PickKeyTarget();
		if ( target )
		{
			KeyValues *kv = new KeyValues( "KeyTyped" );
			kv->SetInt( "unichar", (int)unichar );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}
	void InternalKeyCodeReleased( KeyCode code ) OVERRIDE
	{
		if ( !ValidKey( code ))
			return;
		m_keyDown[code] = false;
		m_keyReleased[code] = true;
		VPANEL target = PickKeyTarget();
		if ( target )
		{
			KeyValues *kv = new KeyValues( "KeyCodeReleased" );
			kv->SetInt( "code", (int)code );
			PostToPanel( target, kv );
			kv->deleteThis();
		}
	}

	HInputContext CreateInputContext() OVERRIDE { return ++m_context; }
	void DestroyInputContext( HInputContext context ) OVERRIDE { (void)context; }
	void AssociatePanelWithInputContext( HInputContext context, VPANEL pRoot ) OVERRIDE
	{
		(void)context; (void)pRoot;
	}
	void ActivateInputContext( HInputContext context ) OVERRIDE { m_activeContext = context; }

	VPANEL GetMouseCapture() OVERRIDE { return m_mouseCapture; }

	bool IsChildOfModalPanel( VPANEL panel ) OVERRIDE
	{
		if ( !m_appModal )
			return true;
		if ( panel == m_appModal )
			return true;
		return VGui2_GetPanelInterface()->HasParent( panel, m_appModal );
	}

	void ResetInputContext( HInputContext context ) OVERRIDE
	{
		(void)context;
		memset( m_mouseDown, 0, sizeof( m_mouseDown ));
		memset( m_keyDown, 0, sizeof( m_keyDown ));
		m_mouseFocus = NULL_HANDLE;
		m_keyFocus = NULL_HANDLE;
		m_mouseCapture = NULL_HANDLE;
	}

private:
	bool ValidMouse( int code ) const { return code >= 0 && code < VGUI2_MAX_MOUSECODE; }
	bool ValidKey( int code ) const { return code >= 0 && code < VGUI2_MAX_KEYCODE; }

	bool IsAnyMouseDown() const
	{
		for ( int i = 0; i < VGUI2_MAX_MOUSECODE; i++ )
			if ( m_mouseDown[i] )
				return true;
		return false;
	}

	void PostToPanel( VPANEL target, KeyValues *kv )
	{
		IPanel *ipanel = VGui2_GetPanelInterface();
		if ( m_appModal && target != m_appModal && !ipanel->HasParent( target, m_appModal ))
			return; // modal surface eats the event
		ipanel->SendMessage( target, kv, NULL_HANDLE );
	}

	VPANEL PickKeyTarget()
	{
		if ( m_appModal )
			return m_appModal;
		if ( m_keyFocus )
			return m_keyFocus;
		if ( m_mouseFocus )
			return m_mouseFocus;
		return NULL_HANDLE;
	}

	VPANEL FindPanelAt( int x, int y )
	{
		IPanel *ipanel = VGui2_GetPanelInterface();
		// front-most top-level panel first (reverse creation order)
		for ( int i = VGui2_GetPanelCount() - 1; i >= 0; i-- )
		{
			VPANEL vp = VGui2_GetPanelHandle( i );
			if ( !vp )
				continue;
			if ( ipanel->GetParent( vp ) != NULL_HANDLE )
				continue;
			if ( !ipanel->IsVisible( vp ))
				continue;
			VPANEL hit = ipanel->IsWithinTraverse( vp, x, y, true );
			if ( hit )
			{
				if ( m_appModal && hit != m_appModal && !ipanel->HasParent( hit, m_appModal ))
					continue;
				return hit;
			}
		}
		return NULL_HANDLE;
	}

	const char *KeyCodeToName( KeyCode code )
	{
		switch ( code )
		{
		case KEY_0: return "0"; case KEY_1: return "1"; case KEY_2: return "2";
		case KEY_3: return "3"; case KEY_4: return "4"; case KEY_5: return "5";
		case KEY_6: return "6"; case KEY_7: return "7"; case KEY_8: return "8";
		case KEY_9: return "9"; case KEY_A: return "a"; case KEY_B: return "b";
		case KEY_C: return "c"; case KEY_D: return "d"; case KEY_E: return "e";
		case KEY_F: return "f"; case KEY_G: return "g"; case KEY_H: return "h";
		case KEY_I: return "i"; case KEY_J: return "j"; case KEY_K: return "k";
		case KEY_L: return "l"; case KEY_M: return "m"; case KEY_N: return "n";
		case KEY_O: return "o"; case KEY_P: return "p"; case KEY_Q: return "q";
		case KEY_R: return "r"; case KEY_S: return "s"; case KEY_T: return "t";
		case KEY_U: return "u"; case KEY_V: return "v"; case KEY_W: return "w";
		case KEY_X: return "x"; case KEY_Y: return "y"; case KEY_Z: return "z";
		case KEY_PAD_0: return "KP_0"; case KEY_PAD_1: return "KP_1";
		case KEY_PAD_2: return "KP_2"; case KEY_PAD_3: return "KP_3";
		case KEY_PAD_4: return "KP_4"; case KEY_PAD_5: return "KP_5";
		case KEY_PAD_6: return "KP_6"; case KEY_PAD_7: return "KP_7";
		case KEY_PAD_8: return "KP_8"; case KEY_PAD_9: return "KP_9";
		case KEY_PAD_DIVIDE: return "KP_DIVIDE"; case KEY_PAD_MULTIPLY: return "KP_MULTIPLY";
		case KEY_PAD_MINUS: return "KP_MINUS"; case KEY_PAD_PLUS: return "KP_PLUS";
		case KEY_PAD_ENTER: return "KP_ENTER"; case KEY_PAD_DECIMAL: return "KP_DECIMAL";
		case KEY_LBRACKET: return "["; case KEY_RBRACKET: return "]";
		case KEY_SEMICOLON: return ";"; case KEY_BACKQUOTE: return "`";
		case KEY_COMMA: return ","; case KEY_PERIOD: return ".";
		case KEY_MINUS: return "-"; case KEY_EQUAL: return "=";
		case KEY_SLASH: return "/"; case KEY_BACKSLASH: return "\\";
		case KEY_APOSTROPHE: return "'";
		case KEY_TAB: return "TAB"; case KEY_ENTER: return "ENTER";
		case KEY_SPACE: return "SPACE"; case KEY_CAPSLOCK: return "CAPSLOCK";
		case KEY_BACKSPACE: return "BACKSPACE"; case KEY_ESCAPE: return "ESCAPE";
		case KEY_INSERT: return "INS"; case KEY_DELETE: return "DEL";
		case KEY_HOME: return "HOME"; case KEY_END: return "END";
		case KEY_PAGEUP: return "PGUP"; case KEY_PAGEDOWN: return "PGDN";
		case KEY_BREAK: return "BREAK";
		case KEY_LSHIFT: return "SHIFT"; case KEY_RSHIFT: return "RSHIFT";
		case KEY_LALT: return "ALT"; case KEY_RALT: return "RALT";
		case KEY_LCONTROL: return "CTRL"; case KEY_RCONTROL: return "RCTRL";
		case KEY_LWIN: return "LWIN"; case KEY_RWIN: return "RWIN";
		case KEY_UP: return "UPARROW"; case KEY_LEFT: return "LEFTARROW";
		case KEY_DOWN: return "DOWNARROW"; case KEY_RIGHT: return "RIGHTARROW";
		case KEY_F1: return "F1"; case KEY_F2: return "F2"; case KEY_F3: return "F3";
		case KEY_F4: return "F4"; case KEY_F5: return "F5"; case KEY_F6: return "F6";
		case KEY_F7: return "F7"; case KEY_F8: return "F8"; case KEY_F9: return "F9";
		case KEY_F10: return "F10"; case KEY_F11: return "F11"; case KEY_F12: return "F12";
		case KEY_NUMLOCK: return "NUMLOCK"; case KEY_SCROLLLOCK: return "SCROLLLOCK";
		default: return NULL;
		}
	}

	bool m_mouseDown[VGUI2_MAX_MOUSECODE];
	bool m_mousePressed[VGUI2_MAX_MOUSECODE];
	bool m_mouseReleased[VGUI2_MAX_MOUSECODE];
	bool m_mouseDouble[VGUI2_MAX_MOUSECODE];
	bool m_keyDown[VGUI2_MAX_KEYCODE];
	bool m_keyPressed[VGUI2_MAX_KEYCODE];
	bool m_keyTyped[VGUI2_MAX_KEYCODE];
	bool m_keyReleased[VGUI2_MAX_KEYCODE];
	VPANEL m_mouseFocus;
	VPANEL m_keyFocus;
	VPANEL m_mouseOver;
	VPANEL m_mouseCapture;
	VPANEL m_appModal;
	HCursor m_cursorOverride;
	int m_cursorX, m_cursorY;
	HInputContext m_context;
	HInputContext m_activeContext;
};

static CInputImpl s_InputImpl;

IInputInternal *VGui2_GetInputInterface() { return &s_InputImpl; }

} // namespace vgui2
