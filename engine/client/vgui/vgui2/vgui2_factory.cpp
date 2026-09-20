// vgui2_factory.cpp - CreateInterface entry point exposing the engine-side
// VGUI2 GoldSource interfaces: Surface, InputInternal, ivgui, Panel,
// Localize, Scheme, System.

#include <cstring>

#include <vgui/ISurface.h>
#include <vgui/IInputInternal.h>
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>
#include <vgui/ISystem.h>
#include <tier1/interface.h>

#include "vgui2_backend.h"
#include "vgui2_internal.h"

namespace vgui2
{

void *VGui2_CreateInterface( const char *name, int *returnCode )
{
	if ( returnCode )
		*returnCode = IFACE_OK;

	// scheme manager needs the surface for font/texture creation
	VGui2_SetSchemeSurface( VGui2_GetSurfaceInterface() );

	if ( !strcmp( name, VGUI_SURFACE_INTERFACE_VERSION_GS ))
		return VGui2_GetSurfaceInterface();
	if ( !strcmp( name, VGUI_INPUTINTERNAL_INTERFACE_VERSION ))
		return VGui2_GetInputInterface();
	if ( !strcmp( name, VGUI_IVGUI_INTERFACE_VERSION_GS ))
		return VGui2_GetVGuiInterface();
	if ( !strcmp( name, VGUI_PANEL_INTERFACE_VERSION_GS ))
		return VGui2_GetPanelInterface();
	if ( !strcmp( name, VGUI_LOCALIZE_INTERFACE_VERSION ))
		return VGui2_GetLocalizeInterface();
	if ( !strcmp( name, VGUI_SCHEME_INTERFACE_VERSION_GS ))
		return VGui2_GetSchemeManagerInterface();
	if ( !strcmp( name, VGUI_SYSTEM_INTERFACE_VERSION_GS ))
		return VGui2_GetSystemInterface();
	if ( !strcmp( name, VGui2_KeyValuesVersion() ))
		return VGui2_GetKeyValuesInterface();
	if ( !strcmp( name, VGui2_SchemeLoaderVersion() ))
		return VGui2_GetSchemeLoaderInterface();

	if ( returnCode )
		*returnCode = IFACE_FAILED;
	return NULL;
}

} // namespace vgui2

// C-ABI entry point for game clients (declared manually on their side).
extern "C" void *VGui2_EngineFactory( const char *name, int *returnCode )
{
	return vgui2::VGui2_CreateInterface( name, returnCode );
}
