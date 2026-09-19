// vgui2_backend_engine.cpp - real engine backend for the VGUI2 GoldSource port.
//
// Wires the vgui2_backend_t interface to engine + ref_api functionality.
// Coordinates are raw screen pixels (refState), matching GoldSource VGUI2
// surface space. Built into the engine (waf); not part of unit tests.

#include "common.h"
#include "client.h"
#include "ref_common.h"
#include "platform/platform.h"
#include "cursor_type.h"
#include "common/const.h"
#include "vgui2/vgui2_backend.h"

//-----------------------------------------------------------------------------
static void ENGINE_GetScreenSize( int *wide, int *tall )
{
	if ( wide ) *wide = refState.width;
	if ( tall ) *tall = refState.height;
}

static double ENGINE_GetTime( void )
{
	return Platform_DoubleTime();
}

static void ENGINE_GetMousePos( int *x, int *y )
{
	Platform_GetMousePos( x, y );
}

static void ENGINE_SetCursor( int cursor )
{
	if ( cursor < 0 || cursor >= dc_last )
		cursor = dc_arrow;
	Platform_SetCursorType((VGUI_DefaultCursor)cursor );
}

static void ENGINE_SetupDrawing( int forRect )
{
	ref.dllFuncs.VGUI_SetupDrawing( forRect ? true : false );
}

static void ENGINE_DrawFilled( float x, float y, float w, float h, int r, int g, int b, int a )
{
	ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, (byte)r, (byte)g, (byte)b, (byte)a );
}

static void ENGINE_DrawTextured( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int tex )
{
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, tex );
}

static int ENGINE_LoadTextureRGBA( const char *name, const unsigned char *rgba, int wide, int tall )
{
	if ( !name || !rgba || wide <= 0 || tall <= 0 )
		return 0;

	rgbdata_t pic;
	memset( &pic, 0, sizeof( pic ));
	pic.width = wide;
	pic.height = tall;
	pic.type = PF_RGBA_32;
	pic.size = (size_t)wide * tall * 4;
	pic.flags = IMAGE_HAS_COLOR | IMAGE_HAS_ALPHA;
	pic.buffer = (byte *)rgba;

	return GL_LoadTextureInternal( name, &pic, TF_IMAGE );
}

static int ENGINE_LoadTextureFile( const char *path, int *wide, int *tall )
{
	if ( wide ) *wide = 0;
	if ( tall ) *tall = 0;
	if ( !path || !path[0] )
		return 0;

	// NULL buffer makes the renderer load and decode the file itself
	int tex = ref.dllFuncs.GL_LoadTexture( path, NULL, 0, TF_IMAGE );
	if ( !tex )
		return 0;
	int w = 0, h = 0;
	R_GetTextureParms( &w, &h, tex );
	if ( wide ) *wide = w;
	if ( tall ) *tall = h;
	return tex;
}

static void ENGINE_UpdateTextureRGBA( int tex, int x, int y, const unsigned char *rgba, int wide, int tall )
{
	if ( !tex || !rgba || wide <= 0 || tall <= 0 )
		return;
	ref.dllFuncs.GL_UpdateTexture( tex, x, y, wide, tall, rgba, PF_RGBA_32 );
}

static void ENGINE_FreeTexture( int tex )
{
	if ( !tex )
		return;
	ref.dllFuncs.GL_FreeTexture((unsigned int)tex );
}

static void ENGINE_GetTextureSize( int tex, int *wide, int *tall )
{
	int w = 0, h = 0;
	if ( tex )
		R_GetTextureParms( &w, &h, tex );
	if ( wide ) *wide = w;
	if ( tall ) *tall = h;
}

//-----------------------------------------------------------------------------
static cl_font_t *ENGINE_PickFont( int tall )
{
	cl_font_t *best = Con_GetFont( 0 );
	int bestDiff = 1 << 30;
	for ( int i = 0; i < 3; i++ )
	{
		cl_font_t *f = Con_GetFont( i );
		if ( !f || !f->valid )
			continue;
		int diff = abs( f->charHeight - tall );
		if ( diff < bestDiff )
		{
			bestDiff = diff;
			best = f;
		}
	}
	return best;
}

static int ENGINE_UTF8Decode( const char **pp )
{
	const byte *s = (const byte *)*pp;
	int ch;
	if ( *s < 0x80 )
	{
		ch = *s++;
	}
	else if (( *s & 0xE0 ) == 0xC0 && s[1] )
	{
		ch = (( *s & 0x1F ) << 6 ) | ( s[1] & 0x3F );
		s += 2;
	}
	else if (( *s & 0xF0 ) == 0xE0 && s[1] && s[2] )
	{
		ch = (( *s & 0x0F ) << 12 ) | (( s[1] & 0x3F ) << 6 ) | ( s[2] & 0x3F );
		s += 3;
	}
	else
	{
		ch = '?';
		s++;
	}
	*pp = (const char *)s;
	return ch;
}

static int ENGINE_DrawTextUTF8( int x, int y, int tall, const char *utf8, int r, int g, int b, int a )
{
	if ( !utf8 || !utf8[0] )
		return 0;
	cl_font_t *font = ENGINE_PickFont( tall );
	if ( !font )
		return 0;
	rgba_t color;
	Vector4Set( color, r, g, b, a );
	return CL_DrawString((float)x, (float)y, utf8, color, font, FONT_DRAW_UTF8 | FONT_DRAW_FORCECOL );
}

static void ENGINE_GetTextSizeUTF8( int tall, const char *utf8, int *wide, int *h )
{
	int w = 0, hh = 0;
	cl_font_t *font = ENGINE_PickFont( tall );
	if ( font && utf8 )
	{
		hh = font->charHeight;
		const char *s = utf8;
		while ( *s )
		{
			int ch = ENGINE_UTF8Decode( &s );
			if ( ch == '\n' )
				continue;
			if ( ch < 0 || ch >= 256 )
				ch = '?';
			w += font->charWidths[ch];
		}
	}
	if ( wide ) *wide = w;
	if ( h ) *h = hh;
}

static int ENGINE_GetCharWidth( int tall, int ch )
{
	cl_font_t *font = ENGINE_PickFont( tall );
	if ( !font )
		return 0;
	if ( ch < 0 || ch >= 256 )
		ch = '?';
	return font->charWidths[ch];
}

//-----------------------------------------------------------------------------
static int ENGINE_LoadFile( const char *path, unsigned char **outBuf )
{
	if ( outBuf ) *outBuf = NULL;
	if ( !path || !path[0] || !outBuf )
		return -1;
	int len = 0;
	byte *buf = COM_LoadFile( path, 0, &len );
	if ( !buf || len <= 0 )
		return -1;
	*outBuf = buf;
	return len;
}

static void ENGINE_FreeFile( unsigned char *buf )
{
	if ( buf )
		COM_FreeFile( buf );
}

static int ENGINE_GetClipboardText( char *buf, int bufLen )
{
	if ( !buf || bufLen <= 0 )
		return 0;
	buf[0] = 0;
	return Platform_GetClipboardText( buf, (size_t)bufLen );
}

static void ENGINE_SetClipboardText( const char *text )
{
	Platform_SetClipboardText( text ? text : "" );
}

static void ENGINE_PlaySound( const char *fileName )
{
	if ( fileName && fileName[0] )
		Cbuf_AddTextf( "play \"%s\"\n", fileName );
}

static void ENGINE_Log( const char *msg )
{
	if ( msg )
		Con_Printf( "%s", msg );
}

static const vgui2_backend_t s_engineBackend =
{
	ENGINE_GetScreenSize,
	ENGINE_GetTime,
	ENGINE_GetMousePos,
	ENGINE_SetCursor,
	ENGINE_SetupDrawing,
	ENGINE_DrawFilled,
	ENGINE_DrawTextured,
	ENGINE_LoadTextureRGBA,
	ENGINE_LoadTextureFile,
	ENGINE_UpdateTextureRGBA,
	ENGINE_FreeTexture,
	ENGINE_GetTextureSize,
	ENGINE_DrawTextUTF8,
	ENGINE_GetTextSizeUTF8,
	ENGINE_GetCharWidth,
	ENGINE_LoadFile,
	ENGINE_FreeFile,
	ENGINE_GetClipboardText,
	ENGINE_SetClipboardText,
	ENGINE_PlaySound,
	ENGINE_Log,
};

extern "C" void VGui2_InitEngineBackend( void )
{
	VGui2_SetBackend( &s_engineBackend );
}

extern "C" void *VGui2_EngineFactory( const char *name, int *returnCode )
{
	// backend is always valid once anyone asks for an interface
	VGui2_SetBackend( &s_engineBackend );
	return vgui2::VGui2_CreateInterface( name, returnCode );
}
