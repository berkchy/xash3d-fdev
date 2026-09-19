// vgui2_scheme.cpp - IScheme + ISchemeManager implementation.

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui/IBorder.h>
#include <vgui/IImage.h>
#include <vgui/IPanel.h>
#include <vgui/VGUI2.h>
#include <Color.h>
#include <KeyValues.h>
#include <tier1/utlvector.h>
#include <tier1/utlstring.h>
#include <tier1/strtools.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

namespace vgui2
{

//-----------------------------------------------------------------------------
class CBorderImpl : public IBorder
{
public:
	CBorderImpl()
	{
		m_inset[0] = m_inset[1] = m_inset[2] = m_inset[3] = 0;
		m_name[0] = 0;
		for ( int i = 0; i < 4; i++ )
			m_sideColor[i] = Color( 0, 0, 0, 255 );
		m_sides = 0xF;
	}
	void Paint( VPANEL panel ) OVERRIDE
	{
		IPanel *ip = VGui2_GetPanelInterface();
		int x0, y0, x1, y1;
		ip->GetClipRect( panel, x0, y0, x1, y1 );
		Paint( x0, y0, x1, y1 );
	}
	void Paint( int x0, int y0, int x1, int y1 ) OVERRIDE
	{
		Paint( x0, y0, x1, y1, -1, 0, 0 );
	}
	void Paint( int x0, int y0, int x1, int y1, int breakSide, int breakStart, int breakStop ) OVERRIDE
	{
		ISurface *surf = VGui2_GetSurfaceInterface();
		if ( !surf )
			return;
		(void)breakStart; (void)breakStop;
		if (( m_sides & ( 1 << IBorder::SIDE_TOP )) && breakSide != IBorder::SIDE_TOP )
		{
			surf->DrawSetColor( m_sideColor[IBorder::SIDE_TOP] );
			surf->DrawFilledRect( x0, y0, x1, y0 + 1 );
		}
		if (( m_sides & ( 1 << IBorder::SIDE_BOTTOM )) && breakSide != IBorder::SIDE_BOTTOM )
		{
			surf->DrawSetColor( m_sideColor[IBorder::SIDE_BOTTOM] );
			surf->DrawFilledRect( x0, y1 - 1, x1, y1 );
		}
		if (( m_sides & ( 1 << IBorder::SIDE_LEFT )) && breakSide != IBorder::SIDE_LEFT )
		{
			surf->DrawSetColor( m_sideColor[IBorder::SIDE_LEFT] );
			surf->DrawFilledRect( x0, y0, x0 + 1, y1 );
		}
		if (( m_sides & ( 1 << IBorder::SIDE_RIGHT )) && breakSide != IBorder::SIDE_RIGHT )
		{
			surf->DrawSetColor( m_sideColor[IBorder::SIDE_RIGHT] );
			surf->DrawFilledRect( x1 - 1, y0, x1, y1 );
		}
	}
	void SetInset( int left, int top, int right, int bottom ) OVERRIDE
	{
		m_inset[0] = left; m_inset[1] = top; m_inset[2] = right; m_inset[3] = bottom;
	}
	void GetInset( int &left, int &top, int &right, int &bottom ) OVERRIDE
	{
		left = m_inset[0]; top = m_inset[1]; right = m_inset[2]; bottom = m_inset[3];
	}
	void ApplySchemeSettings( IScheme *pScheme, KeyValues *inResourceData ) OVERRIDE
	{
		if ( !inResourceData )
			return;
		const char *inset = inResourceData->GetString( "inset", NULL );
		if ( inset )
		{
			int l = 0, t = 0, r = 0, b = 0;
			sscanf( inset, "%d %d %d %d", &l, &t, &r, &b );
			SetInset( l, t, r, b );
		}
		static const char *sideNames[4] = { "Left", "Top", "Right", "Bottom" };
		for ( int i = 0; i < 4; i++ )
		{
			KeyValues *side = inResourceData->FindKey( sideNames[i], false );
			if ( !side )
				continue;
			KeyValues *layer = side->GetFirstSubKey();
			if ( !layer )
				continue;
			const char *colorName = layer->GetString( "color", NULL );
			if ( colorName && pScheme )
			{
				m_sideColor[i] = pScheme->GetColor( colorName, m_sideColor[i] );
				m_sides |= ( 1 << i );
			}
		}
		const char *name = inResourceData->GetName();
		if ( name )
			Q_strncpy( m_name, name, sizeof( m_name ));
	}
	const char *GetName() OVERRIDE { return m_name; }
	void SetName( const char *name ) OVERRIDE
	{
		Q_strncpy( m_name, name ? name : "", sizeof( m_name ));
	}

private:
	int m_inset[4];
	Color m_sideColor[4];
	int m_sides;
	char m_name[64];
};

//-----------------------------------------------------------------------------
class CImageImpl : public IImage
{
public:
	CImageImpl( int texId, int wide, int tall ) : m_tex( texId ), m_wide( wide ), m_tall( tall ), m_x( 0 ), m_y( 0 )
	{
		m_color = Color( 255, 255, 255, 255 );
	}
	void Paint() OVERRIDE
	{
		ISurface *surf = VGui2_GetSurfaceInterface();
		if ( !surf || !m_tex )
			return;
		surf->DrawSetColor( m_color );
		surf->DrawSetTexture( m_tex );
		surf->DrawTexturedRect( m_x, m_y, m_x + m_wide, m_y + m_tall );
	}
	void SetPos( int x, int y ) OVERRIDE { m_x = x; m_y = y; }
	void GetContentSize( int &wide, int &tall ) OVERRIDE { wide = m_wide; tall = m_tall; }
	void GetSize( int &wide, int &tall ) OVERRIDE { wide = m_wide; tall = m_tall; }
	void SetSize( int wide, int tall ) OVERRIDE { m_wide = wide; m_tall = tall; }
	void SetColor( Color col ) OVERRIDE { m_color = col; }

private:
	int m_tex;
	int m_wide, m_tall;
	int m_x, m_y;
	Color m_color;
};

//-----------------------------------------------------------------------------
struct SchemeColor { CUtlString name; Color color; };
struct SchemeFont { CUtlString name; HFont font; };

class CSchemeImpl : public IScheme
{
public:
	CSchemeImpl() {}
	~CSchemeImpl()
	{
		for ( int i = 0; i < m_borders.Count(); i++ )
			delete m_borders[i].border;
	}

	const char *GetResourceString( const char *stringName ) OVERRIDE
	{
		for ( int i = 0; i < m_settings.Count(); i++ )
		{
			if ( !Q_stricmp( m_settings[i].name, stringName ? stringName : "" ))
				return m_settings[i].value;
		}
		return "";
	}
	IBorder *GetBorder( const char *borderName ) OVERRIDE
	{
		for ( int i = 0; i < m_borders.Count(); i++ )
		{
			if ( !Q_stricmp( m_borders[i].name, borderName ? borderName : "" ))
				return m_borders[i].border;
		}
		return NULL;
	}
	HFont GetFont( const char *fontName, bool proportional ) OVERRIDE
	{
		(void)proportional;
		for ( int i = 0; i < m_fonts.Count(); i++ )
		{
			if ( !Q_stricmp( m_fonts[i].name, fontName ? fontName : "" ))
				return m_fonts[i].font;
		}
		return EnsureDefaultFont();
	}
	Color GetColor( const char *colorName, Color defaultColor ) OVERRIDE
	{
		for ( int i = 0; i < m_colors.Count(); i++ )
		{
			if ( !Q_stricmp( m_colors[i].name, colorName ? colorName : "" ))
				return m_colors[i].color;
		}
		return defaultColor;
	}
	HFont GetFontEx( const char *fontName, bool proportional, bool hdProportional ) OVERRIDE
	{
		(void)hdProportional;
		return GetFont( fontName, proportional );
	}

	void LoadFromKeyValues( KeyValues *schemeRoot, ISurface *surface )
	{
		if ( !schemeRoot )
			return;
		// Colors + BaseSettings
		ParseColorSection( schemeRoot->FindKey( "Colors", false ));
		ParseColorSection( schemeRoot->FindKey( "BaseSettings", false ));
		// Fonts
		KeyValues *fonts = schemeRoot->FindKey( "Fonts", false );
		if ( fonts && surface )
		{
			for ( KeyValues *f = fonts->GetFirstSubKey(); f; f = f->GetNextKey() )
			{
				KeyValues *glyph = f->GetFirstSubKey();
				if ( !glyph )
					glyph = f;
				const char *fontFile = glyph->GetString( "name", "Arial" );
				int tall = glyph->GetInt( "tall", 14 );
				int weight = glyph->GetInt( "weight", 400 );
				int blur = glyph->GetInt( "blur", 0 );
				int scanlines = glyph->GetInt( "scanlines", 0 );
				int flags = 0;
				if ( glyph->GetInt( "antialias", 1 )) flags |= ISurface::FONTFLAG_ANTIALIAS;
				if ( glyph->GetInt( "underline", 0 )) flags |= ISurface::FONTFLAG_UNDERLINE;
				if ( glyph->GetInt( "strikeout", 0 )) flags |= ISurface::FONTFLAG_STRIKEOUT;
				if ( glyph->GetInt( "symbol", 0 )) flags |= ISurface::FONTFLAG_SYMBOL;
				if ( glyph->GetInt( "dropshadow", 0 )) flags |= ISurface::FONTFLAG_DROPSHADOW;
				if ( glyph->GetInt( "outline", 0 )) flags |= ISurface::FONTFLAG_OUTLINE;
				if ( glyph->GetInt( "additive", 0 )) flags |= ISurface::FONTFLAG_ADDITIVE;
				if ( blur > 0 ) flags |= ISurface::FONTFLAG_GAUSSIANBLUR;
				HFont hf = surface->CreateFont();
				surface->AddGlyphSetToFont( hf, fontFile, tall, weight, blur, scanlines, flags, 0, 0 );
				SchemeFont sf;
				sf.name = f->GetName();
				sf.font = hf;
				m_fonts.AddToTail( sf );
			}
		}
		// Borders
		KeyValues *borders = schemeRoot->FindKey( "Borders", false );
		if ( borders )
		{
			for ( KeyValues *b = borders->GetFirstSubKey(); b; b = b->GetNextKey() )
			{
				CBorderImpl *border = new CBorderImpl();
				border->ApplySchemeSettings( this, b );
				BorderEntry e;
				e.name = b->GetName();
				e.border = border;
				m_borders.AddToTail( e );
			}
		}
		// keep settings for GetResourceString
		m_settings.Purge();
		KeyValues *base = schemeRoot->FindKey( "BaseSettings", false );
		if ( base )
		{
			for ( KeyValues *k = base->GetFirstSubKey(); k; k = k->GetNextKey() )
			{
				SettingEntry e;
				e.name = k->GetName();
				e.value = k->GetString();
				m_settings.AddToTail( e );
			}
		}
	}

private:
	struct BorderEntry { CUtlString name; CBorderImpl *border; };
	struct SettingEntry { CUtlString name; CUtlString value; };

	void ParseColorSection( KeyValues *section )
	{
		if ( !section )
			return;
		for ( KeyValues *k = section->GetFirstSubKey(); k; k = k->GetNextKey() )
		{
			const char *v = k->GetString();
			if ( !v || !v[0] )
				continue;
			int r = 255, g = 255, b = 255, a = 255;
			if ( sscanf( v, "%d %d %d %d", &r, &g, &b, &a ) < 3 )
				continue; // named reference; resolved lazily is out of scope
			bool replaced = false;
			for ( int i = 0; i < m_colors.Count(); i++ )
			{
				if ( !Q_stricmp( m_colors[i].name, k->GetName() ))
				{
					m_colors[i].color = Color( r, g, b, a );
					replaced = true;
					break;
				}
			}
			if ( !replaced )
			{
				SchemeColor sc;
				sc.name = k->GetName();
				sc.color = Color( r, g, b, a );
				m_colors.AddToTail( sc );
			}
		}
	}
	HFont EnsureDefaultFont()
	{
		ISurface *surface = VGui2_GetSurfaceInterface();
		if ( !surface )
			return INVALID_FONT;
		for ( int i = 0; i < m_fonts.Count(); i++ )
		{
			if ( !Q_stricmp( m_fonts[i].name, "Default" ))
				return m_fonts[i].font;
		}
		HFont hf = surface->CreateFont();
		surface->AddGlyphSetToFont( hf, "Arial", 14, 400, 0, 0, ISurface::FONTFLAG_ANTIALIAS, 0, 0 );
		SchemeFont sf;
		sf.name = "Default";
		sf.font = hf;
		m_fonts.AddToTail( sf );
		return hf;
	}

	CUtlVector<SchemeColor> m_colors;
	CUtlVector<SchemeFont> m_fonts;
	CUtlVector<BorderEntry> m_borders;
	CUtlVector<SettingEntry> m_settings;
};

//-----------------------------------------------------------------------------
struct SchemeSlot { CUtlString tag; CSchemeImpl *scheme; };
struct ImageSlot { CUtlString name; CImageImpl *image; HTexture tex; };

class CSchemeManagerImpl : public ISchemeManagerEx
{
public:
	CSchemeManagerImpl() : m_surface( NULL ) {}
	~CSchemeManagerImpl() { Shutdown( true ); }

	void SetSurface( ISurface *surface ) { m_surface = surface; }

	HScheme LoadSchemeFromFile( const char *fileName, const char *tag ) OVERRIDE
	{
		if ( !fileName || !fileName[0] )
			return NULL_HANDLE;
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->loadFile )
			return NULL_HANDLE;
		unsigned char *buf = NULL;
		int len = b->loadFile( fileName, &buf );
		if ( len <= 0 || !buf )
			return NULL_HANDLE;
		KeyValues *kv = new KeyValues( "Scheme" );
		bool ok = kv->LoadFromBuffer( fileName, (const char *)buf, NULL, NULL );
		b->freeFile( buf );
		if ( !ok )
		{
			kv->deleteThis();
			return NULL_HANDLE;
		}
		KeyValues *root = kv->GetFirstSubKey();
		if ( !root )
			root = kv;
		CSchemeImpl *scheme = new CSchemeImpl();
		scheme->LoadFromKeyValues( root, m_surface );
		kv->deleteThis();

		HScheme handle = (HScheme)( m_schemes.Count() + 1 );
		SchemeSlot slot;
		slot.tag = tag ? tag : fileName;
		slot.scheme = scheme;
		m_schemes.AddToTail( slot );
		return handle;
	}

	void ReloadSchemes() OVERRIDE {}

	HScheme GetDefaultScheme() OVERRIDE
	{
		return m_schemes.Count() > 0 ? 1 : NULL_HANDLE;
	}
	HScheme GetScheme( const char *tag ) OVERRIDE
	{
		for ( int i = 0; i < m_schemes.Count(); i++ )
		{
			if ( !Q_stricmp( m_schemes[i].tag, tag ? tag : "" ))
				return (HScheme)( i + 1 );
		}
		return NULL_HANDLE;
	}

	IImage *GetImage( const char *imageName, bool hardwareFiltered ) OVERRIDE
	{
		(void)hardwareFiltered;
		for ( int i = 0; i < m_images.Count(); i++ )
		{
			if ( !Q_stricmp( m_images[i].name, imageName ? imageName : "" ))
				return m_images[i].image;
		}
		HTexture tex = GetImageID( imageName, hardwareFiltered );
		if ( !tex || !m_surface )
			return NULL;
		int wide = 0, tall = 0;
		m_surface->DrawGetTextureSize( (int)tex, wide, tall );
		CImageImpl *img = new CImageImpl( (int)tex, wide, tall );
		ImageSlot slot;
		slot.name = imageName ? imageName : "";
		slot.image = img;
		slot.tex = tex;
		m_images.AddToTail( slot );
		return img;
	}
	HTexture GetImageID( const char *imageName, bool hardwareFiltered ) OVERRIDE
	{
		(void)hardwareFiltered;
		if ( !imageName || !imageName[0] || !m_surface )
			return NULL_HANDLE;
		for ( int i = 0; i < m_images.Count(); i++ )
		{
			if ( !Q_stricmp( m_images[i].name, imageName ))
				return m_images[i].tex;
		}
		int id = m_surface->CreateNewTextureID( false );
		m_surface->DrawSetTextureFile( id, imageName, false, true );
		if ( !m_surface->IsTextureIDValid( id ))
			return NULL_HANDLE;
		// cache the id even before an IImage wraps it
		ImageSlot slot;
		slot.name = imageName;
		slot.image = NULL;
		slot.tex = (HTexture)id;
		m_images.AddToTail( slot );
		return (HTexture)id;
	}

	IScheme *GetIScheme( HScheme scheme ) OVERRIDE
	{
		if ( !scheme || scheme > (HScheme)m_schemes.Count() )
			return NULL;
		return m_schemes[scheme - 1].scheme;
	}

	void Shutdown( bool full ) OVERRIDE
	{
		(void)full;
		for ( int i = 0; i < m_schemes.Count(); i++ )
			delete m_schemes[i].scheme;
		m_schemes.Purge();
		for ( int i = 0; i < m_images.Count(); i++ )
			delete m_images[i].image;
		m_images.Purge();
	}

	int GetProportionalScaledValue( int normalizedValue ) OVERRIDE
	{
		return (int)( normalizedValue * GetProportionalScale() );
	}
	int GetProportionalNormalizedValue( int scaledValue ) OVERRIDE
	{
		float s = GetProportionalScale();
		return s != 0.0f ? (int)( scaledValue / s ) : scaledValue;
	}
	float GetProportionalScale() OVERRIDE
	{
		int baseW = 640, baseH = 480;
		if ( m_surface )
			m_surface->GetProportionalBase( baseW, baseH );
		const vgui2_backend_t *b = VGui2_GetBackend();
		int sw = 640, sh = 480;
		if ( b && b->getScreenSize )
			b->getScreenSize( &sw, &sh );
		if ( baseH <= 0 )
			return 1.0f;
		return (float)sh / (float)baseH;
	}
	int GetHDProportionalScaledValue( int normalizedValue ) OVERRIDE
	{
		return (int)( normalizedValue * GetHDScale() );
	}
	int GetHDProportionalNormalizedValue( int scaledValue ) OVERRIDE
	{
		float s = GetHDScale();
		return s != 0.0f ? (int)( scaledValue / s ) : scaledValue;
	}

	HScheme LoadSchemeFromFileEx( VPANEL sizingPanel, const char *fileName, const char *tag ) OVERRIDE
	{
		(void)sizingPanel;
		return LoadSchemeFromFile( fileName, tag );
	}
	int GetProportionalScaledValueEx( HScheme scheme, int normalizedValue ) OVERRIDE
	{
		(void)scheme;
		return GetProportionalScaledValue( normalizedValue );
	}
	int GetProportionalNormalizedValueEx( HScheme scheme, int scaledValue ) OVERRIDE
	{
		(void)scheme;
		return GetProportionalNormalizedValue( scaledValue );
	}
	HScheme LoadSchemeFromFilePath( const char *fileName, const char *pathID, const char *tag ) OVERRIDE
	{
		(void)pathID;
		return LoadSchemeFromFile( fileName, tag );
	}

private:
	float GetHDScale()
	{
		int baseW = 1280, baseH = 720;
		if ( m_surface )
			m_surface->GetHDProportionalBase( baseW, baseH );
		const vgui2_backend_t *b = VGui2_GetBackend();
		int sw = 1280, sh = 720;
		if ( b && b->getScreenSize )
			b->getScreenSize( &sw, &sh );
		if ( baseH <= 0 )
			return 1.0f;
		return (float)sh / (float)baseH;
	}

	ISurface *m_surface;
	CUtlVector<SchemeSlot> m_schemes;
	CUtlVector<ImageSlot> m_images;
};

static CSchemeManagerImpl s_SchemeManagerImpl;

ISchemeManager *VGui2_GetSchemeManagerInterface() { return &s_SchemeManagerImpl; }
void VGui2_SetSchemeSurface( ISurface *surface ) { s_SchemeManagerImpl.SetSurface( surface ); }

} // namespace vgui2
