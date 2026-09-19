// vgui2_panel.cpp - IPanel + IVGui implementation for the VGUI2 GoldSource port.
//
// Panel store owned here; VPANEL handles are 1-based indices into the store
// (VPANEL is 32-bit, so raw pointers cannot be used on LP64).

#include <cstdio>
#include <cstring>
#include <cstdarg>

#include <vgui/IPanel.h>
#include <vgui/IVGui.h>
#include <vgui/IClientPanel.h>
#include <vgui/IInputInternal.h>
#include <vgui/IScheme.h>
#include <KeyValues.h>
#include <tier1/utlvector.h>
#include <tier1/strtools.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

namespace vgui2
{

//-----------------------------------------------------------------------------
struct VPanelData
{
	bool used;
	int x, y;
	int wide, tall;
	int minWide, minTall;
	int zpos;
	bool visible;
	VPANEL parent;
	CUtlVector<VPANEL> children;
	IClientPanel *client;
	HScheme scheme;
	bool proportional;
	bool autoDelete;
	bool kbInput;
	bool mouseInput;
	bool enabled;
	bool popup;
	bool popupVisible;
	int insetLeft, insetTop, insetRight, insetBottom;
	void *plat;
	char name[64];
	char className[64];
	char module[32];
};

//-----------------------------------------------------------------------------
class CPanelSystem
{
public:
	CPanelSystem()
	{
		memset( &m_tick, 0, sizeof( m_tick ));
		memset( &m_msg, 0, sizeof( m_msg ));
	}

	VPanelData *Get( VPANEL vpanel )
	{
		if ( !vpanel || vpanel > (VPANEL)m_panels.Count() )
			return NULL;
		VPanelData *p = &m_panels[vpanel - 1];
		return p->used ? p : NULL;
	}

	VPANEL Alloc()
	{
		for ( int i = 0; i < m_panels.Count(); i++ )
		{
			if ( !m_panels[i].used )
			{
				InitData( &m_panels[i] );
				return (VPANEL)( i + 1 );
			}
		}
		int idx = m_panels.AddToTail();
		InitData( &m_panels[idx] );
		return (VPANEL)( idx + 1 );
	}

	void Free( VPANEL vpanel )
	{
		VPanelData *p = Get( vpanel );
		if ( !p )
			return;
		// free children first
		while ( p->children.Count() > 0 )
			Free( p->children[0] );
		// unlink from parent
		if ( p->parent )
		{
			VPanelData *par = Get( p->parent );
			if ( par )
				par->children.FindAndRemove( vpanel );
		}
		RemoveTickSignal( vpanel );
		p->children.Purge();
		p->used = false;
		p->client = NULL;
		if ( VGui2_GetInputInterface() )
			VGui2_GetInputInterface()->PanelDeleted( vpanel );
	}

	void GetAbsPos( VPANEL vpanel, int &x, int &y )
	{
		x = 0; y = 0;
		VPanelData *p = Get( vpanel );
		while ( p )
		{
			x += p->x; y += p->y;
			if ( !p->parent )
				break;
			p = Get( p->parent );
		}
	}

	//--- tick signals ---
	struct TickEntry { VPANEL panel; int intervalMs; double nextFire; };
	struct MsgEntry
	{
		VPANEL target;
		VPANEL from;
		double fireTime;
		KeyValues *params;
	};

	void AddTickSignal( VPANEL vpanel, int intervalMs )
	{
		RemoveTickSignal( vpanel );
		TickEntry e;
		e.panel = vpanel;
		e.intervalMs = intervalMs;
		e.nextFire = Now() + intervalMs / 1000.0;
		m_tick.AddToTail( e );
	}

	void RemoveTickSignal( VPANEL vpanel )
	{
		for ( int i = 0; i < m_tick.Count(); i++ )
		{
			if ( m_tick[i].panel == vpanel )
			{
				m_tick.Remove( i );
				return;
			}
		}
	}

	void PostMessage( VPANEL target, KeyValues *params, VPANEL from, float delay )
	{
		if ( !params )
			return;
		MsgEntry e;
		e.target = target;
		e.from = from;
		e.fireTime = Now() + delay;
		e.params = params->MakeCopy();
		m_msg.AddToTail( e );
	}

	void RunFrame( IPanel *panelIface )
	{
		double now = Now();
		// ticks
		for ( int i = 0; i < m_tick.Count(); i++ )
		{
			if ( now >= m_tick[i].nextFire )
			{
				VPanelData *p = Get( m_tick[i].panel );
				if ( p && p->client )
				{
					KeyValues *kv = new KeyValues( "Tick" );
					p->client->OnMessage( kv, NULL_HANDLE );
					kv->deleteThis();
				}
				if ( m_tick[i].intervalMs <= 0 )
					m_tick[i].nextFire = now + 0.05;
				else
					m_tick[i].nextFire = now + m_tick[i].intervalMs / 1000.0;
			}
		}
		// delayed messages
		for ( int i = 0; i < m_msg.Count(); )
		{
			if ( now >= m_msg[i].fireTime )
			{
				VPanelData *p = Get( m_msg[i].target );
				if ( p && p->client )
					p->client->OnMessage( m_msg[i].params, m_msg[i].from );
				m_msg[i].params->deleteThis();
				m_msg.Remove( i );
			}
			else i++;
		}
	}

	void Shutdown()
	{
		for ( int i = 0; i < m_msg.Count(); i++ )
			m_msg[i].params->deleteThis();
		m_msg.Purge();
		m_tick.Purge();
	}

	int PanelCount() const { return m_panels.Count(); }

private:
	void InitData( VPanelData *p )
	{
		memset( p, 0, sizeof( *p ));
		p->used = true;
		p->visible = true;
		p->enabled = true;
		p->kbInput = true;
		p->mouseInput = true;
		p->popupVisible = true;
	}

	double Now()
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		return b && b->getTime ? b->getTime() : 0.0;
	}

	CUtlVector<VPanelData> m_panels;
	CUtlVector<TickEntry> m_tick;
	CUtlVector<MsgEntry> m_msg;
};

static CPanelSystem s_Panels;

//-----------------------------------------------------------------------------
class CPanelImpl : public IPanel
{
public:
	void Init( VPANEL vguiPanel, IClientPanel *panel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p )
			return;
		p->client = panel;
		if ( panel )
		{
			Q_strncpy( p->name, panel->GetName() ? panel->GetName() : "", sizeof( p->name ));
			Q_strncpy( p->className, panel->GetClassName() ? panel->GetClassName() : "", sizeof( p->className ));
			p->scheme = panel->GetScheme();
			p->proportional = panel->IsProportional();
			p->autoDelete = panel->IsAutoDeleteSet();
		}
	}

	void SetPos( VPANEL vguiPanel, int x, int y ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { p->x = x; p->y = y; }
	}
	void GetPos( VPANEL vguiPanel, int &x, int &y ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { x = p->x; y = p->y; } else { x = y = 0; }
	}
	void SetSize( VPANEL vguiPanel, int wide, int tall ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p )
			return;
		if ( p->wide != wide || p->tall != tall )
		{
			p->wide = wide; p->tall = tall;
			if ( p->client )
				p->client->OnSizeChanged( wide, tall );
		}
	}
	void GetSize( VPANEL vguiPanel, int &wide, int &tall ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { wide = p->wide; tall = p->tall; } else { wide = tall = 0; }
	}
	void SetMinimumSize( VPANEL vguiPanel, int wide, int tall ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { p->minWide = wide; p->minTall = tall; }
	}
	void GetMinimumSize( VPANEL vguiPanel, int &wide, int &tall ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { wide = p->minWide; tall = p->minTall; } else { wide = tall = 0; }
	}
	void SetZPos( VPANEL vguiPanel, int z ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->zpos = z;
	}
	int GetZPos( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->zpos : 0;
	}

	void GetAbsPos( VPANEL vguiPanel, int &x, int &y ) OVERRIDE
	{
		s_Panels.GetAbsPos( vguiPanel, x, y );
	}
	void GetClipRect( VPANEL vguiPanel, int &x0, int &y0, int &x1, int &y1 ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p ) { x0 = y0 = x1 = y1 = 0; return; }
		s_Panels.GetAbsPos( vguiPanel, x0, y0 );
		x1 = x0 + p->wide; y1 = y0 + p->tall;
		// clip against ancestors
		VPANEL par = p->parent;
		while ( par )
		{
			VPanelData *q = s_Panels.Get( par );
			if ( !q )
				break;
			int px, py;
			s_Panels.GetAbsPos( par, px, py );
			if ( px > x0 ) x0 = px;
			if ( py > y0 ) y0 = py;
			if ( px + q->wide < x1 ) x1 = px + q->wide;
			if ( py + q->tall < y1 ) y1 = py + q->tall;
			par = q->parent;
		}
		// insets
		x0 += p->insetLeft; y0 += p->insetTop;
		x1 -= p->insetRight; y1 -= p->insetBottom;
	}
	void SetInset( VPANEL vguiPanel, int left, int top, int right, int bottom ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { p->insetLeft = left; p->insetTop = top; p->insetRight = right; p->insetBottom = bottom; }
	}
	void GetInset( VPANEL vguiPanel, int &left, int &top, int &right, int &bottom ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) { left = p->insetLeft; top = p->insetTop; right = p->insetRight; bottom = p->insetBottom; }
		else { left = top = right = bottom = 0; }
	}

	void SetVisible( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->visible = state;
	}
	bool IsVisible( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->visible : false;
	}
	void SetParent( VPANEL vguiPanel, VPANEL newParent ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || vguiPanel == newParent )
			return;
		if ( p->parent )
		{
			VPanelData *old = s_Panels.Get( p->parent );
			if ( old )
				old->children.FindAndRemove( vguiPanel );
		}
		p->parent = newParent;
		if ( newParent )
		{
			VPanelData *n = s_Panels.Get( newParent );
			if ( n )
			{
				n->children.AddToTail( vguiPanel );
				if ( p->client )
					n->client ? n->client->OnChildAdded( vguiPanel ) : (void)0;
			}
			else p->parent = NULL_HANDLE;
		}
	}
	int GetChildCount( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->children.Count() : 0;
	}
	VPANEL GetChild( VPANEL vguiPanel, int index ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || index < 0 || index >= p->children.Count() )
			return NULL_HANDLE;
		return p->children[index];
	}
	VPANEL GetParent( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->parent : NULL_HANDLE;
	}
	void MoveToFront( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || !p->parent )
			return;
		VPanelData *par = s_Panels.Get( p->parent );
		if ( !par )
			return;
		par->children.FindAndRemove( vguiPanel );
		par->children.AddToTail( vguiPanel );
	}
	void MoveToBack( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || !p->parent )
			return;
		VPanelData *par = s_Panels.Get( p->parent );
		if ( !par )
			return;
		par->children.FindAndRemove( vguiPanel );
		par->children.InsertBefore( 0, vguiPanel );
	}
	bool HasParent( VPANEL vguiPanel, VPANEL potentialParent ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		while ( p && p->parent )
		{
			if ( p->parent == potentialParent )
				return true;
			p = s_Panels.Get( p->parent );
		}
		return false;
	}
	bool IsPopup( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->popup : false;
	}
	void SetPopup( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->popup = state;
	}

	bool Render_GetPopupVisible( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->popupVisible : false;
	}
	void Render_SetPopupVisible( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->popupVisible = state;
	}

	HScheme GetScheme( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->scheme : NULL_HANDLE;
	}
	bool IsProportional( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->proportional : false;
	}
	bool IsAutoDeleteSet( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->autoDelete : false;
	}
	void DeletePanel( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p )
			return;
		IClientPanel *client = p->client;
		p->client = NULL;
		s_Panels.Free( vguiPanel );
		if ( client )
			client->DeletePanel();
	}

	void SetKeyBoardInputEnabled( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->kbInput = state;
	}
	void SetMouseInputEnabled( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->mouseInput = state;
	}
	bool IsKeyBoardInputEnabled( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->kbInput : false;
	}
	bool IsMouseInputEnabled( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->mouseInput : false;
	}

	void Solve( VPANEL vguiPanel ) OVERRIDE
	{
		// layout is resolved by the client panels themselves; nothing cached here
		(void)vguiPanel;
	}

	const char *GetName( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->name : "";
	}
	const char *GetClassName( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->className : "";
	}

	void SendMessage( VPANEL vguiPanel, KeyValues *params, VPANEL ifromPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client && params )
			p->client->OnMessage( params, ifromPanel );
	}

	void Think( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->Think();
	}
	void PerformApplySchemeSettings( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->PerformApplySchemeSettings();
	}
	void PaintTraverse( VPANEL vguiPanel, bool forceRepaint, bool allowForce ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client && p->visible )
			p->client->PaintTraverse( forceRepaint, allowForce );
	}
	void Repaint( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->Repaint();
	}
	VPANEL IsWithinTraverse( VPANEL vguiPanel, int x, int y, bool traversePopups ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || !p->visible || !p->mouseInput )
			return NULL_HANDLE;
		int ax, ay;
		s_Panels.GetAbsPos( vguiPanel, ax, ay );
		// children front-to-back (last child is front-most)
		for ( int i = p->children.Count() - 1; i >= 0; i-- )
		{
			VPANEL hit = IsWithinTraverse( p->children[i], x, y, traversePopups );
			if ( hit )
				return hit;
		}
		if ( x >= ax && y >= ay && x < ax + p->wide && y < ay + p->tall )
			return vguiPanel;
		return NULL_HANDLE;
	}
	void OnChildAdded( VPANEL vguiPanel, VPANEL child ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->OnChildAdded( child );
	}
	void OnSizeChanged( VPANEL vguiPanel, int newWide, int newTall ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->OnSizeChanged( newWide, newTall );
	}

	void InternalFocusChanged( VPANEL vguiPanel, bool lost ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->InternalFocusChanged( lost );
	}
	bool RequestInfo( VPANEL vguiPanel, KeyValues *outputData ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			return p->client->RequestInfo( outputData );
		return false;
	}
	void RequestFocus( VPANEL vguiPanel, int direction ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			p->client->RequestFocus( direction );
	}
	bool RequestFocusPrev( VPANEL vguiPanel, VPANEL existingPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			return p->client->RequestFocusPrev( existingPanel );
		return false;
	}
	bool RequestFocusNext( VPANEL vguiPanel, VPANEL existingPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			return p->client->RequestFocusNext( existingPanel );
		return false;
	}
	VPANEL GetCurrentKeyFocus( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			return p->client->GetCurrentKeyFocus();
		return NULL_HANDLE;
	}
	int GetTabPosition( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p && p->client )
			return p->client->GetTabPosition();
		return 0;
	}

	SurfacePlat *Plat( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? (SurfacePlat *)p->plat : NULL;
	}
	void SetPlat( VPANEL vguiPanel, SurfacePlat *plat ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->plat = plat;
	}

	Panel *GetPanel( VPANEL vguiPanel, const char *destinationModule ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( !p || !p->client )
			return NULL;
		if ( destinationModule && destinationModule[0] && strcmp( p->module, destinationModule ) != 0 )
			return NULL;
		return (Panel *)p->client->QueryInterface( ICLIENTPANEL_STANDARD_INTERFACE );
	}

	bool IsEnabled( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->enabled : false;
	}
	void SetEnabled( VPANEL vguiPanel, bool state ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p ) p->enabled = state;
	}

	IClientPanel *Client( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->client : NULL;
	}
	const char *GetModuleName( VPANEL vguiPanel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		return p ? p->module : "";
	}

	void SetModuleName( VPANEL vguiPanel, const char *module )
	{
		VPanelData *p = s_Panels.Get( vguiPanel );
		if ( p )
			Q_strncpy( p->module, module ? module : "", sizeof( p->module ));
	}
};

//-----------------------------------------------------------------------------
class CVGuiImpl : public IVGui
{
public:
	CVGuiImpl() : m_running( false ), m_sleep( true ), m_context( 1 ), m_activeContext( 1 ) {}

	bool Init( CreateInterfaceFn *factoryList, int numFactories ) OVERRIDE
	{
		(void)factoryList; (void)numFactories;
		return true;
	}
	void Shutdown() OVERRIDE
	{
		m_running = false;
		s_Panels.Shutdown();
	}
	void Start() OVERRIDE { m_running = true; }
	void Stop() OVERRIDE { m_running = false; }
	bool IsRunning() OVERRIDE { return m_running; }
	void RunFrame() OVERRIDE
	{
		if ( !m_running )
			return;
		s_Panels.RunFrame( NULL );
	}
	void ShutdownMessage( unsigned int shutdownID ) OVERRIDE
	{
		KeyValues *kv = new KeyValues( "ShutdownRequest" );
		kv->SetInt( "id", (int)shutdownID );
		// broadcast to all top-level panels
		for ( int i = 0; i < s_Panels.PanelCount(); i++ )
		{
			VPANEL vp = (VPANEL)( i + 1 );
			VPanelData *p = s_Panels.Get( vp );
			if ( p && !p->parent && p->client )
				p->client->OnMessage( kv, NULL_HANDLE );
		}
		kv->deleteThis();
	}

	VPANEL AllocPanel() OVERRIDE { return s_Panels.Alloc(); }
	void FreePanel( VPANEL panel ) OVERRIDE { s_Panels.Free( panel ); }

	void DPrintf( const char *format, ... ) OVERRIDE
	{
		char buf[1024];
		va_list args;
		va_start( args, format );
		Q_vsnprintf( buf, sizeof( buf ), format, args );
		va_end( args );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->log )
			b->log( buf );
	}
	void DPrintf2( const char *format, ... ) OVERRIDE
	{
		char buf[1024];
		va_list args;
		va_start( args, format );
		Q_vsnprintf( buf, sizeof( buf ), format, args );
		va_end( args );
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( b && b->log )
			b->log( buf );
	}
	void SpewAllActivePanelNames() OVERRIDE
	{
		const vgui2_backend_t *b = VGui2_GetBackend();
		if ( !b || !b->log )
			return;
		char buf[160];
		for ( int i = 0; i < s_Panels.PanelCount(); i++ )
		{
			VPANEL vp = (VPANEL)( i + 1 );
			VPanelData *p = s_Panels.Get( vp );
			if ( p )
			{
				Q_snprintf( buf, sizeof( buf ), "panel %u: %s (%s)\n", vp, p->name, p->className );
				b->log( buf );
			}
		}
	}

	HPanel PanelToHandle( VPANEL panel ) OVERRIDE { return (HPanel)panel; }
	VPANEL HandleToPanel( HPanel index ) OVERRIDE { return (VPANEL)index; }
	void MarkPanelForDeletion( VPANEL panel ) OVERRIDE
	{
		VPanelData *p = s_Panels.Get( panel );
		if ( p && p->autoDelete )
			PostMessage( panel, new KeyValues( "DeletePanel" ), NULL_HANDLE, 0.0f );
	}

	void AddTickSignal( VPANEL panel, int intervalMilliseconds ) OVERRIDE
	{
		s_Panels.AddTickSignal( panel, intervalMilliseconds );
	}
	void RemoveTickSignal( VPANEL panel ) OVERRIDE
	{
		s_Panels.RemoveTickSignal( panel );
	}

	void PostMessage( VPANEL target, KeyValues *params, VPANEL from, float delaySeconds ) OVERRIDE
	{
		s_Panels.PostMessage( target, params, from, delaySeconds );
	}

	HContext CreateContext() OVERRIDE { return ++m_context; }
	void DestroyContext( HContext context ) OVERRIDE { (void)context; }
	void AssociatePanelWithContext( HContext context, VPANEL pRoot ) OVERRIDE
	{
		(void)context; (void)pRoot;
	}
	void ActivateContext( HContext context ) OVERRIDE { m_activeContext = context; }

	void SetSleep( bool state ) OVERRIDE { m_sleep = state; }
	bool GetShouldVGuiControlSleep() OVERRIDE { return m_sleep; }

private:
	bool m_running;
	bool m_sleep;
	HContext m_context;
	HContext m_activeContext;
};

static CPanelImpl s_PanelImpl;
static CVGuiImpl s_VGuiImpl;

IPanel *VGui2_GetPanelInterface() { return &s_PanelImpl; }
IVGui *VGui2_GetVGuiInterface() { return &s_VGuiImpl; }
CPanelSystem *VGui2_GetPanelSystem() { return &s_Panels; }

int VGui2_GetPanelCount() { return s_Panels.PanelCount(); }
VPANEL VGui2_GetPanelHandle( int index )
{
	if ( index < 0 || index >= s_Panels.PanelCount() )
		return NULL_HANDLE;
	return s_Panels.Get( (VPANEL)( index + 1 )) ? (VPANEL)( index + 1 ) : NULL_HANDLE;
}

} // namespace vgui2
