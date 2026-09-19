// vgui2_surface.cpp - ISurface implementation for the VGUI2 GoldSource port.
//
// Drawing goes through the vgui2_backend_t supplied by the engine
// (ref_api GL primitives + engine font system). All coordinates are
// screen space; panels already pass absolute coordinates.

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <vgui/ISurface.h>
#include <vgui/IPanel.h>
#include <vgui/IInput.h>
#include <vgui/IInputInternal.h>
#include <color.h>
#include <tier1/utlvector.h>
#include <tier1/utlstring.h>
#include <tier1/strtools.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

namespace vgui2
{

struct SurfaceTexture
{
	bool used;
	int engineTex;
	int wide, tall;
	bool procedural;
};

struct SurfaceFont
{
	bool used;
	char name[64];
	int tall;
	int weight;
	int blur;
	int scanlines;
	int flags;
};

class CSurfaceImpl : public ISurface
{
public:
	CSurfaceImpl()
	{
		m_embedded = NULL_HANDLE;
		m_restrict = NULL_HANDLE;
		m_modal = NULL_HANDLE;
		m_topFocus = NULL_HANDLE;
		m_topmost = NULL_HANDLE;
		m_notify = NULL_HANDLE;
		m_hasFocus = true;
		m_cursorVisible = true;
		m_cursorLocked = false;
		m_translateExt = false;
		m_allowJS = false;
		m_supportsEsc = true;
		m_baseW = 640; m_baseH = 480;
		m_hdBaseW = 1280; m_hdBaseH = 720;
		m_lang[0] = 0;
		Q_strncpy( m_lang, "english", sizeof( m_lang ));
		m_drawColor = Color( 255, 255, 255, 255 );
		m_textColor = Color( 255, 255, 255, 255 );
		m_textX = 0; m_textY = 0;
		m_textFont = INVALID_FONT;
		m_curTexture = 0;
		m_cursor = 0;
		m_curX = 0; m_curY = 0;
		m_hasSetCursor = false;
	}

	void Shutdown() OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->freeTexture )
		{
			for ( int i = 0; i < m_textures.Count(); i++ )
			{
				if ( m_textures[i].used && m_textures[i].engineTex )
					b->freeTexture( m_textures[i].engineTex );
			}
		}
		m_textures.Purge();
		m_fonts.Purge();
		m_popups.Purge();
	}

	void RunFrame() OVERRIDE {}

	VPANEL GetEmbeddedPanel() OVERRIDE { return m_embedded; }
	void SetEmbeddedPanel( VPANEL pPanel ) OVERRIDE { m_embedded = pPanel; }

	void PushMakeCurrent( VPANEL panel, bool useInsets ) OVERRIDE
	{
		(void)useInsets;
		m_makeCurrent.AddToTail( panel );
	}
	void PopMakeCurrent( VPANEL panel ) OVERRIDE
	{
		(void)panel;
		if ( m_makeCurrent.Count() > 0 )
			m_makeCurrent.Remove( m_makeCurrent.Count() - 1 );
	}

	void DrawSetColor( int r, int g, int b, int a ) OVERRIDE
	{
		m_drawColor = Color( r, g, b, a );
	}
	void DrawSetColor( Color col ) OVERRIDE { m_drawColor = col; }

	void DrawFilledRect( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->setupDrawing || !b->drawFilled )
			return;
		if ( x1 < x0 ) { int t = x0; x0 = x1; x1 = t; }
		if ( y1 < y0 ) { int t = y0; y0 = y1; y1 = t; }
		b->setupDrawing( 1 );
		b->drawFilled((float)x0, (float)y0, (float)( x1 - x0 ), (float)( y1 - y0 ),
			m_drawColor[0], m_drawColor[1], m_drawColor[2], m_drawColor[3] );
	}
	void DrawOutlinedRect( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		DrawLine( x0, y0, x1, y0 );
		DrawLine( x1, y0, x1, y1 );
		DrawLine( x1, y1, x0, y1 );
		DrawLine( x0, y1, x0, y0 );
	}

	void DrawLine( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->setupDrawing || !b->drawFilled )
			return;
		b->setupDrawing( 1 );
		int r = m_drawColor[0], g = m_drawColor[1], bl = m_drawColor[2], a = m_drawColor[3];
		if ( y0 == y1 )
		{
			if ( x1 < x0 ) { int t = x0; x0 = x1; x1 = t; }
			b->drawFilled((float)x0, (float)y0, (float)( x1 - x0 + 1 ), 1.0f, r, g, bl, a );
		}
		else if ( x0 == x1 )
		{
			if ( y1 < y0 ) { int t = y0; y0 = y1; y1 = t; }
			b->drawFilled((float)x0, (float)y0, 1.0f, (float)( y1 - y0 + 1 ), r, g, bl, a );
		}
		else
		{
			// diagonal: step along the major axis
			int dx = x1 - x0, dy = y1 - y0;
			int steps = abs( dx ) > abs( dy ) ? abs( dx ) : abs( dy );
			for ( int i = 0; i <= steps; i++ )
			{
				int x = x0 + dx * i / steps;
				int y = y0 + dy * i / steps;
				b->drawFilled((float)x, (float)y, 1.0f, 1.0f, r, g, bl, a );
			}
		}
	}
	void DrawPolyLine( int *px, int *py, int numPoints ) OVERRIDE
	{
		if ( !px || !py || numPoints < 2 )
			return;
		for ( int i = 1; i < numPoints; i++ )
			DrawLine( px[i-1], py[i-1], px[i], py[i] );
	}

	void DrawSetTextFont( HFont font ) OVERRIDE { m_textFont = font; }
	void DrawSetTextColor( int r, int g, int b, int a ) OVERRIDE
	{
		m_textColor = Color( r, g, b, a );
	}
	void DrawSetTextColor( Color col ) OVERRIDE { m_textColor = col; }
	void DrawSetTextPos( int x, int y ) OVERRIDE { m_textX = x; m_textY = y; }
	void DrawGetTextPos( int &x, int &y ) OVERRIDE { x = m_textX; y = m_textY; }
	void DrawPrintText( const wchar_t *text, int textLen ) OVERRIDE
	{
		if ( !text || textLen <= 0 )
			return;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->drawTextUTF8 )
			return;
		char utf8[2048];
		WCharToUTF8( text, textLen, utf8, sizeof( utf8 ));
		int tall = FontTall( m_textFont );
		b->setupDrawing( 0 );
		int w = b->drawTextUTF8( m_textX, m_textY, tall, utf8,
			m_textColor[0], m_textColor[1], m_textColor[2], m_textColor[3] );
		m_textX += w;
	}
	void DrawUnicodeChar( wchar_t wch ) OVERRIDE
	{
		wchar_t tmp[2] = { wch, 0 };
		DrawPrintText( tmp, 1 );
	}
	void DrawUnicodeCharAdd( wchar_t wch ) OVERRIDE
	{
		DrawUnicodeChar( wch );
	}

	void DrawFlushText() OVERRIDE {}
	IHTML *CreateHTMLWindow( vgui2::IHTMLEvents *events, VPANEL context ) OVERRIDE
	{
		(void)events; (void)context;
		return NULL;
	}
	void PaintHTMLWindow( vgui2::IHTML *htmlwin ) OVERRIDE { (void)htmlwin; }
	void DeleteHTMLWindow( IHTML *htmlwin ) OVERRIDE { (void)htmlwin; }

	void DrawSetTextureFile( int id, const char *filename, int hardwareFilter, bool forceReload ) OVERRIDE
	{
		(void)hardwareFilter; (void)forceReload;
		SurfaceTexture *t = GetTexture( id );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !t || !b || !b->loadTextureFile || !filename )
			return;
		if ( t->engineTex && b->freeTexture )
			b->freeTexture( t->engineTex );
		t->engineTex = b->loadTextureFile( filename, &t->wide, &t->tall );
	}
	void DrawSetTextureRGBA( int id, const unsigned char *rgba, int wide, int tall, int hardwareFilter, bool forceReload ) OVERRIDE
	{
		(void)hardwareFilter; (void)forceReload;
		SurfaceTexture *t = GetTexture( id );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !t || !b || !b->loadTextureRGBA || !rgba || wide <= 0 || tall <= 0 )
			return;
		if ( t->engineTex && b->freeTexture )
			b->freeTexture( t->engineTex );
		char name[32];
		Q_snprintf( name, sizeof( name ), "*vgui2_%d", id );
		t->engineTex = b->loadTextureRGBA( name, rgba, wide, tall );
		t->wide = wide; t->tall = tall;
	}
	void DrawSetTexture( int id ) OVERRIDE { m_curTexture = id; }
	void DrawGetTextureSize( int id, int &wide, int &tall ) OVERRIDE
	{
		SurfaceTexture *t = GetTexture( id );
		if ( t ) { wide = t->wide; tall = t->tall; }
		else { wide = tall = 0; }
	}
	void DrawTexturedRect( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		DrawTexturedRectUV( x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f );
	}
	bool IsTextureIDValid( int id ) OVERRIDE
	{
		SurfaceTexture *t = GetTexture( id );
		return t && t->engineTex != 0;
	}

	int CreateNewTextureID( bool procedural ) OVERRIDE
	{
		for ( int i = 0; i < m_textures.Count(); i++ )
		{
			if ( !m_textures[i].used )
			{
				m_textures[i].used = true;
				m_textures[i].engineTex = 0;
				m_textures[i].wide = m_textures[i].tall = 0;
				m_textures[i].procedural = procedural;
				return i + 1;
			}
		}
		SurfaceTexture t;
		t.used = true; t.engineTex = 0;
		t.wide = t.tall = 0; t.procedural = procedural;
		m_textures.AddToTail( t );
		return m_textures.Count();
	}

	void GetScreenSize( int &wide, int &tall ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->getScreenSize )
			b->getScreenSize( &wide, &tall );
		else { wide = 640; tall = 480; }
	}
	void SetAsTopMost( VPANEL panel, bool state ) OVERRIDE
	{
		(void)state;
		BringToFront( panel );
	}
	void BringToFront( VPANEL panel ) OVERRIDE
	{
		m_popups.FindAndRemove( panel );
		m_popups.AddToTail( panel );
		VGui2_GetPanelInterface()->MoveToFront( panel );
	}
	void SetForegroundWindow( VPANEL panel ) OVERRIDE { BringToFront( panel ); }
	void SetPanelVisible( VPANEL panel, bool state ) OVERRIDE
	{
		VGui2_GetPanelInterface()->SetVisible( panel, state );
	}
	void SetMinimized( VPANEL panel, bool state ) OVERRIDE { (void)panel; (void)state; }
	bool IsMinimized( VPANEL panel ) OVERRIDE { (void)panel; return false; }
	void FlashWindow( VPANEL panel, bool state ) OVERRIDE { (void)panel; (void)state; }
	void SetTitle( VPANEL panel, const wchar_t *title ) OVERRIDE { (void)panel; (void)title; }
	void SetAsToolBar( VPANEL panel, bool state ) OVERRIDE { (void)panel; (void)state; }

	void CreatePopup( VPANEL panel, bool minimised, bool showTaskbarIcon, bool disabled, bool mouseInput, bool kbInput ) OVERRIDE
	{
		(void)minimised; (void)showTaskbarIcon; (void)disabled;
		IPanel *ip = VGui2_GetPanelInterface();
		ip->SetPopup( panel, true );
		ip->SetMouseInputEnabled( panel, mouseInput );
		ip->SetKeyBoardInputEnabled( panel, kbInput );
		if ( m_popups.Find( panel ) == m_popups.InvalidIndex() )
			m_popups.AddToTail( panel );
	}
	void SwapBuffers( VPANEL panel ) OVERRIDE { (void)panel; }
	void Invalidate( VPANEL panel ) OVERRIDE { (void)panel; }
	void SetCursor( HCursor cursor ) OVERRIDE
	{
		m_cursor = cursor;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->setCursor )
			b->setCursor((int)cursor );
	}
	bool IsCursorVisible() OVERRIDE { return m_cursorVisible && !m_cursorLocked; }
	void ApplyChanges() OVERRIDE {}
	bool IsWithin( int x, int y ) OVERRIDE
	{
		int w, h;
		GetScreenSize( w, h );
		return x >= 0 && y >= 0 && x < w && y < h;
	}
	bool HasFocus() OVERRIDE { return m_hasFocus; }

	bool SupportsFeature( SurfaceFeature_e feature ) OVERRIDE
	{
		switch ( feature )
		{
		case ANTIALIASED_FONTS: return true;
		case ESCAPE_KEY: return true;
		default: return false;
		}
	}

	void RestrictPaintToSinglePanel( VPANEL panel ) OVERRIDE { m_restrict = panel; }

	void SetModalPanel( VPANEL panel ) OVERRIDE { m_modal = panel; }
	VPANEL GetModalPanel() OVERRIDE { return m_modal; }

	void UnlockCursor() OVERRIDE { m_cursorLocked = false; }
	void LockCursor() OVERRIDE { m_cursorLocked = true; }
	void SetTranslateExtendedKeys( bool state ) OVERRIDE { m_translateExt = state; }
	VPANEL GetTopmostPopup() OVERRIDE
	{
		return m_popups.Count() > 0 ? m_popups[m_popups.Count() - 1] : NULL_HANDLE;
	}

	void SetTopLevelFocus( VPANEL panel ) OVERRIDE
	{
		m_topFocus = panel;
		m_hasFocus = true;
	}

	HFont CreateFont() OVERRIDE
	{
		for ( int i = 0; i < m_fonts.Count(); i++ )
		{
			if ( !m_fonts[i].used )
			{
				InitFont( &m_fonts[i] );
				return (HFont)( i + 1 );
			}
		}
		SurfaceFont f;
		InitFont( &f );
		m_fonts.AddToTail( f );
		return (HFont)m_fonts.Count();
	}

	bool AddGlyphSetToFont( HFont font, const char *windowsFontName, int tall, int weight, int blur, int scanlines, int flags, int lowRange, int highRange ) OVERRIDE
	{
		(void)lowRange; (void)highRange;
		SurfaceFont *f = GetFont( font );
		if ( !f )
			return false;
		Q_strncpy( f->name, windowsFontName ? windowsFontName : "", sizeof( f->name ));
		f->tall = tall > 0 ? tall : 14;
		f->weight = weight;
		f->blur = blur;
		f->scanlines = scanlines;
		f->flags = flags;
		return true;
	}

	bool AddCustomFontFile( const char *fontFileName ) OVERRIDE
	{
		(void)fontFileName;
		return false;
	}

	int GetFontTall( HFont font ) OVERRIDE
	{
		SurfaceFont *f = GetFont( font );
		return f ? f->tall : 0;
	}
	void GetCharABCwide( HFont font, int ch, int &a, int &b, int &c ) OVERRIDE
	{
		a = 0; b = GetCharacterWidth( font, ch ); c = 0;
	}
	int GetCharacterWidth( HFont font, int ch ) OVERRIDE
	{
		SurfaceFont *f = GetFont( font );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !f || !b || !b->getCharWidth )
			return 0;
		return b->getCharWidth( f->tall, ch );
	}
	void GetTextSize( HFont font, const wchar_t *text, int &wide, int &tall ) OVERRIDE
	{
		SurfaceFont *f = GetFont( font );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !f || !b || !b->getTextSizeUTF8 || !text )
		{
			wide = tall = 0;
			return;
		}
		char utf8[2048];
		WCharToUTF8( text, (int)WCharLen( text ), utf8, sizeof( utf8 ));
		int h = 0;
		b->getTextSizeUTF8( f->tall, utf8, &wide, &h );
		tall = h;
	}

	VPANEL GetNotifyPanel() OVERRIDE { return m_notify; }
	void SetNotifyIcon( VPANEL context, HTexture icon, VPANEL panelToReceiveMessages, const char *text ) OVERRIDE
	{
		(void)icon; (void)panelToReceiveMessages; (void)text;
		m_notify = context;
	}

	void PlaySound( const char *fileName ) OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->playSound )
			b->playSound( fileName ? fileName : "" );
	}

	int GetPopupCount() OVERRIDE { return m_popups.Count(); }
	VPANEL GetPopup( int index ) OVERRIDE
	{
		if ( index < 0 || index >= m_popups.Count() )
			return NULL_HANDLE;
		return m_popups[index];
	}
	bool ShouldPaintChildPanel( VPANEL childPanel ) OVERRIDE
	{
		if ( !m_restrict )
			return true;
		if ( childPanel == m_restrict )
			return true;
		return VGui2_GetPanelInterface()->HasParent( childPanel, m_restrict );
	}
	bool RecreateContext( VPANEL panel ) OVERRIDE { (void)panel; return true; }
	void AddPanel( VPANEL panel ) OVERRIDE { (void)panel; }
	void ReleasePanel( VPANEL panel ) OVERRIDE { (void)panel; }
	void MovePopupToFront( VPANEL panel ) OVERRIDE { BringToFront( panel ); }
	void MovePopupToBack( VPANEL panel ) OVERRIDE
	{
		m_popups.FindAndRemove( panel );
		m_popups.InsertBefore( 0, panel );
		VGui2_GetPanelInterface()->MoveToBack( panel );
	}

	void SolveTraverse( VPANEL panel, bool forceApplySchemeSettings ) OVERRIDE
	{
		(void)forceApplySchemeSettings;
		VGui2_GetPanelInterface()->Solve( panel );
	}
	void PaintTraverse( VPANEL panel ) OVERRIDE
	{
		if ( !ShouldPaintChildPanel( panel ))
			return;
		VGui2_GetPanelInterface()->PaintTraverse( panel, false, true );
	}

	void EnableMouseCapture( VPANEL panel, bool state ) OVERRIDE
	{
		VGui2_GetInputInterface()->SetMouseCapture( state ? panel : NULL_HANDLE );
	}

	void GetWorkspaceBounds( int &x, int &y, int &wide, int &tall ) OVERRIDE
	{
		x = 0; y = 0;
		GetScreenSize( wide, tall );
	}

	void GetAbsoluteWindowBounds( int &x, int &y, int &wide, int &tall ) OVERRIDE
	{
		GetWorkspaceBounds( x, y, wide, tall );
	}

	void GetProportionalBase( int &width, int &height ) OVERRIDE
	{
		width = m_baseW; height = m_baseH;
	}

	void CalculateMouseVisible() OVERRIDE
	{
		m_cursorVisible = m_popups.Count() > 0 || m_modal != NULL_HANDLE;
	}
	bool NeedKBInput() OVERRIDE
	{
		IPanel *ip = VGui2_GetPanelInterface();
		for ( int i = 0; i < m_popups.Count(); i++ )
		{
			if ( ip->IsVisible( m_popups[i] ) && ip->IsKeyBoardInputEnabled( m_popups[i] ))
				return true;
		}
		return false;
	}

	bool HasCursorPosFunctions() OVERRIDE { return true; }
	void SurfaceGetCursorPos( int &x, int &y ) OVERRIDE
	{
		if ( m_hasSetCursor )
		{
			x = m_curX; y = m_curY;
			return;
		}
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->getMousePos )
			b->getMousePos( &x, &y );
		else { x = 0; y = 0; }
	}
	void SurfaceSetCursorPos( int x, int y ) OVERRIDE
	{
		m_curX = x; m_curY = y;
		m_hasSetCursor = true;
	}

	void DrawTexturedPolygon( VGuiVertex *pVertices, int n ) OVERRIDE
	{
		if ( !pVertices || n < 3 )
			return;
		// scanline fill (even-odd), ignoring UVs
		int minY = pVertices[0].y, maxY = pVertices[0].y;
		for ( int i = 1; i < n; i++ )
		{
			if ( pVertices[i].y < minY ) minY = pVertices[i].y;
			if ( pVertices[i].y > maxY ) maxY = pVertices[i].y;
		}
		for ( int y = minY; y <= maxY; y++ )
		{
			int nodes[64];
			int count = 0;
			for ( int i = 0, j = n - 1; i < n && count < 64; j = i++ )
			{
				int yi = pVertices[i].y, yj = pVertices[j].y;
				if (( yi < y && yj >= y ) || ( yj < y && yi >= y ))
				{
					int xi = pVertices[i].x, xj = pVertices[j].x;
					nodes[count++] = xi + ( y - yi ) * ( xj - xi ) / ( yj - yi );
				}
			}
			// sort
			for ( int i = 0; i < count - 1; i++ )
				for ( int j = i + 1; j < count; j++ )
					if ( nodes[j] < nodes[i] )
					{
						int t = nodes[i]; nodes[i] = nodes[j]; nodes[j] = t;
					}
			for ( int i = 0; i + 1 < count; i += 2 )
				DrawFilledRect( nodes[i], y, nodes[i+1], y + 1 );
		}
	}
	int GetFontAscent( HFont font, wchar_t wch ) OVERRIDE
	{
		(void)wch;
		return GetFontTall( font );
	}

	void SetAllowHTMLJavaScript( bool state ) OVERRIDE { m_allowJS = state; }

	void SetLanguage( const char *pchLang ) OVERRIDE
	{
		Q_strncpy( m_lang, pchLang ? pchLang : "english", sizeof( m_lang ));
	}
	const char *GetLanguage() OVERRIDE { return m_lang; }

	bool DeleteTextureByID( int id ) OVERRIDE
	{
		SurfaceTexture *t = GetTexture( id );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !t )
			return false;
		if ( t->engineTex && b && b->freeTexture )
			b->freeTexture( t->engineTex );
		t->used = false;
		t->engineTex = 0;
		return true;
	}

	void DrawUpdateRegionTextureBGRA( int nTextureID, int x, int y, const unsigned char *pchData, int wide, int tall ) OVERRIDE
	{
		SurfaceTexture *t = GetTexture( nTextureID );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !t || !t->engineTex || !b || !b->updateTextureRGBA || !pchData )
			return;
		SwizzleBGRA( pchData, wide, tall, m_swizzle );
		b->updateTextureRGBA( t->engineTex, x, y, m_swizzle.Count() ? &m_swizzle[0] : pchData, wide, tall );
	}

	void DrawSetTextureBGRA( int id, const unsigned char *pchData, int wide, int tall ) OVERRIDE
	{
		if ( !pchData || wide <= 0 || tall <= 0 )
			return;
		SwizzleBGRA( pchData, wide, tall, m_swizzle );
		DrawSetTextureRGBA( id, m_swizzle.Count() ? &m_swizzle[0] : pchData, wide, tall, false, true );
	}

	void CreateBrowser( vgui2::VPANEL panel, IHTMLResponses *pBrowser, bool bPopupWindow, const char *pchUserAgentIdentifier ) OVERRIDE
	{
		(void)panel; (void)pBrowser; (void)bPopupWindow; (void)pchUserAgentIdentifier;
	}
	void RemoveBrowser( vgui2::VPANEL panel, IHTMLResponses *pBrowser ) OVERRIDE
	{
		(void)panel; (void)pBrowser;
	}
	IHTMLChromeController *AccessChromeHTMLController() OVERRIDE { return NULL; }

	void DrawTexturedRectAdd( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		DrawTexturedRect( x0, y0, x1, y1 );
	}
	void SetSupportsEsc( bool bSupportsEsc ) OVERRIDE { m_supportsEsc = bSupportsEsc; }
	int GetFontBlur( vgui2::HFont font ) OVERRIDE
	{
		SurfaceFont *f = GetFont( font );
		return f ? f->blur : 0;
	}
	bool IsAdditive( vgui2::HFont font ) OVERRIDE
	{
		SurfaceFont *f = GetFont( font );
		return f ? ( f->flags & FONTFLAG_ADDITIVE ) != 0 : false;
	}
	void SetProportionalBase( int width, int height ) OVERRIDE
	{
		m_baseW = width; m_baseH = height;
	}

	void GetHDProportionalBase( int &width, int &height ) OVERRIDE
	{
		width = m_hdBaseW; height = m_hdBaseH;
	}
	void SetHDProportionalBase( int nWidth, int nHeight ) OVERRIDE
	{
		m_hdBaseW = nWidth; m_hdBaseH = nHeight;
	}

private:
	void DrawTexturedRectUV( int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1 )
	{
		SurfaceTexture *t = GetTexture( m_curTexture );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !t || !t->engineTex || !b || !b->setupDrawing || !b->drawTextured )
			return;
		if ( x1 < x0 ) { int tt = x0; x0 = x1; x1 = tt; }
		if ( y1 < y0 ) { int tt = y0; y0 = y1; y1 = tt; }
		b->setupDrawing( 1 );
		b->drawTextured((float)x0, (float)y0, (float)( x1 - x0 ), (float)( y1 - y0 ),
			s0, t0, s1, t1, t->engineTex );
	}
	SurfaceTexture *GetTexture( int id )
	{
		if ( id <= 0 || id > m_textures.Count() )
			return NULL;
		SurfaceTexture *t = &m_textures[id - 1];
		return t->used ? t : NULL;
	}
	SurfaceFont *GetFont( HFont font )
	{
		if ( font == INVALID_FONT || font > (HFont)m_fonts.Count() )
			return NULL;
		SurfaceFont *f = &m_fonts[font - 1];
		return f->used ? f : NULL;
	}
	int FontTall( HFont font )
	{
		SurfaceFont *f = GetFont( font );
		return f ? f->tall : 14;
	}
	void InitFont( SurfaceFont *f )
	{
		f->used = true;
		f->name[0] = 0;
		f->tall = 14; f->weight = 400;
		f->blur = 0; f->scanlines = 0; f->flags = 0;
	}
	static size_t WCharLen( const wchar_t *s )
	{
		size_t n = 0;
		while ( s && s[n] ) n++;
		return n;
	}
	static void WCharToUTF8( const wchar_t *src, int srcLen, char *dst, int dstSize )
	{
		if ( !dst || dstSize <= 0 )
			return;
		int o = 0;
		if ( src )
		{
			for ( int i = 0; i < srcLen && src[i] && o + 1 < dstSize; i++ )
			{
				unsigned int ch = (unsigned int)src[i];
				if ( ch < 0x80 )
				{
					dst[o++] = (char)ch;
				}
				else if ( ch < 0x800 && o + 2 < dstSize )
				{
					dst[o++] = (char)( 0xC0 | ( ch >> 6 ));
					dst[o++] = (char)( 0x80 | ( ch & 0x3F ));
				}
				else if ( o + 3 < dstSize )
				{
					dst[o++] = (char)( 0xE0 | ( ch >> 12 ));
					dst[o++] = (char)( 0x80 | (( ch >> 6 ) & 0x3F ));
					dst[o++] = (char)( 0x80 | ( ch & 0x3F ));
				}
				else break;
			}
		}
		dst[o] = 0;
	}
	static void SwizzleBGRA( const unsigned char *src, int wide, int tall, CUtlVector<unsigned char> &out )
	{
		int n = wide * tall;
		out.SetCount( n * 4 );
		for ( int i = 0; i < n; i++ )
		{
			out[i*4+0] = src[i*4+2];
			out[i*4+1] = src[i*4+1];
			out[i*4+2] = src[i*4+0];
			out[i*4+3] = src[i*4+3];
		}
	}

	CUtlVector<SurfaceTexture> m_textures;
	CUtlVector<SurfaceFont> m_fonts;
	CUtlVector<VPANEL> m_popups;
	CUtlVector<VPANEL> m_makeCurrent;
	CUtlVector<unsigned char> m_swizzle;
	VPANEL m_embedded;
	VPANEL m_restrict;
	VPANEL m_modal;
	VPANEL m_topFocus;
	VPANEL m_topmost;
	VPANEL m_notify;
	bool m_hasFocus;
	bool m_cursorVisible;
	bool m_cursorLocked;
	bool m_translateExt;
	bool m_allowJS;
	bool m_supportsEsc;
	int m_baseW, m_baseH;
	int m_hdBaseW, m_hdBaseH;
	char m_lang[32];
	Color m_drawColor;
	Color m_textColor;
	int m_textX, m_textY;
	HFont m_textFont;
	int m_curTexture;
	HCursor m_cursor;
	int m_curX, m_curY;
	bool m_hasSetCursor;
};

static CSurfaceImpl s_SurfaceImpl;

ISurface *VGui2_GetSurfaceInterface() { return &s_SurfaceImpl; }

} // namespace vgui2
