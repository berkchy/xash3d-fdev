// vgui2_base.cpp - backend pointer storage.

#include "vgui2_backend.h"

static const vgui2_backend_t *s_backend = 0;

void VGui2_SetBackend( const vgui2_backend_t *backend )
{
	s_backend = backend;
}

const vgui2_backend_t *VGui2_GetBackend( void )
{
	return s_backend;
}
