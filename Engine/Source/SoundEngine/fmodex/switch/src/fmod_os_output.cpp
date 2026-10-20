/*
    Nintendo Switch port of fmodex/win32/src/fmod_os_output.cpp (wowdecomp): the audout output is the only output, and
    the default for FMOD_OUTPUTTYPE_AUTODETECT.
*/

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_output.h"
#include "fmod_output_audout.h"
#include "fmod_pluginfactory.h"

FMOD_RESULT FMOD_OS_Output_Register(FMOD::PluginFactory *pluginfactory)
{
    pluginfactory->registerOutput(FMOD::OutputAudout::getDescriptionEx());
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Output_GetDefault(FMOD_OUTPUTTYPE *outputtype)
{
    if (!outputtype)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *outputtype = FMOD_OUTPUTTYPE_AUDOUT;
    return FMOD_OK;
}
