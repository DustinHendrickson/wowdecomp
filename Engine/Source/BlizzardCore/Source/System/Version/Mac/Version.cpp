// macOS implementation of Blizzard::Version (Win64/Version.cpp).
//
// Version resources are a Windows executable feature. The one caller reads Launcher.exe's version for the component
// list it sends at Battle.net login and sends 0 when there is none, as for a client without the launcher.

#include "Engine/Source/BlizzardCore/Source/System/Version/Version.h"

bool Blizzard::Version::GetFileVersion(const char* path, FileVersion& version) {
    version.m_major = 0;
    version.m_minor = 0;
    version.m_build = 0;
    version.m_revision = 0;

    return false;
}
