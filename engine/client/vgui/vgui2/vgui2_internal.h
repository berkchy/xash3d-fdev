// vgui2_internal.h - cross-file accessors for the VGUI2 GoldSource port.
// Only used by the interface implementations, never by game code.

#ifndef VGUI2_INTERNAL_H
#define VGUI2_INTERNAL_H

#include <vgui/VGUI2.h>
#include <tier1/interface.h>

namespace vgui2
{

class IPanel;
class IVGui;
class IInputInternal;
class ISurface;
class ISchemeManager;
class ILocalize;
class ISystem;

IPanel *VGui2_GetPanelInterface();
IVGui *VGui2_GetVGuiInterface();
IInputInternal *VGui2_GetInputInterface();
ISurface *VGui2_GetSurfaceInterface();
ISchemeManager *VGui2_GetSchemeManagerInterface();
ILocalize *VGui2_GetLocalizeInterface();
ISystem *VGui2_GetSystemInterface();

// panel enumeration for input routing (vgui2_panel.cpp)
int VGui2_GetPanelCount();
VPANEL VGui2_GetPanelHandle( int index );

// scheme <-> surface wiring (vgui2_scheme.cpp)
void VGui2_SetSchemeSurface( ISurface *surface );

// parse a scheme from a memory buffer (vgui2_scheme.cpp)
HScheme VGui2_LoadSchemeFromBuffer( const char *buffer, const char *tag );

// KeyValues003 system (vgui2_keyvalues.cpp)
const char *VGui2_KeyValuesVersion();
IBaseInterface *VGui2_GetKeyValuesInterface();

// factory entry point (vgui2_factory.cpp)
void *VGui2_CreateInterface( const char *name, int *returnCode );

} // namespace vgui2

#endif // VGUI2_INTERNAL_H
