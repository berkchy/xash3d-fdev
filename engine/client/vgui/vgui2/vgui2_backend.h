// vgui2_backend.h - engine abstraction for the VGUI2 GoldSource port.
//
// The interface implementations in this directory must not include engine
// headers directly, so they can be unit-tested outside the engine.
// The real engine provides a backend in vgui2_backend_engine.cpp; the
// test harness provides a stub backend. All coordinates are screen space.

#ifndef VGUI2_BACKEND_H
#define VGUI2_BACKEND_H

typedef struct vgui2_backend_s
{
	// screen
	void (*getScreenSize)( int *wide, int *tall );

	// time in seconds (monotonic)
	double (*getTime)( void );

	// mouse in screen space
	void (*getMousePos)( int *x, int *y );
	void (*setCursor)( int cursor );

	// 2D drawing setup: forRect=1 -> textured/rect mode, 0 -> text mode
	void (*setupDrawing)( int forRect );

	// untextured quad
	void (*drawFilled)( float x, float y, float w, float h, int r, int g, int b, int a );

	// textured quad using engine texture handle
	void (*drawTextured)( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, int tex );

	// texture management, returns engine texture handle (>0) or 0 on failure
	int (*loadTextureRGBA)( const char *name, const unsigned char *rgba, int wide, int tall );
	int (*loadTextureFile)( const char *path, int *wide, int *tall );
	void (*updateTextureRGBA)( int tex, int x, int y, const unsigned char *rgba, int wide, int tall );
	void (*freeTexture)( int tex );
	void (*getTextureSize)( int tex, int *wide, int *tall );

	// text through the engine font system; tall selects font size
	int (*drawTextUTF8)( int x, int y, int tall, const char *utf8, int r, int g, int b, int a );
	void (*getTextSizeUTF8)( int tall, const char *utf8, int *wide, int *h );
	int (*getCharWidth)( int tall, int ch );

	// file loading for schemes/localization, returns length or -1
	int (*loadFile)( const char *path, unsigned char **outBuf );
	void (*freeFile)( unsigned char *buf );

	// clipboard (text only)
	int (*getClipboardText)( char *buf, int bufLen );
	void (*setClipboardText)( const char *text );

	// misc
	void (*playSound)( const char *fileName );
	void (*log)( const char *msg );
} vgui2_backend_t;

// Implemented by the backend provider, consumed by the interface impls.
void VGui2_SetBackend( const vgui2_backend_t *backend );
const vgui2_backend_t *VGui2_GetBackend( void );

#endif // VGUI2_BACKEND_H
