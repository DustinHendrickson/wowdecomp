// macOS implementation of Battlenet::Library (Win64/Battlenet.cpp) on dlopen. WindowsLibrary keeps its name and holds
// the dlopen handle; ordinal lookups have no macOS equivalent.

#include <dlfcn.h>
#include <new>
#include "WoW/Source/Battlenet/Battlenet.h"

void* Battlenet::WindowsLibrary::GetProc(const char* name) {
    return dlsym(this->m_module, name);
}

void* Battlenet::WindowsLibrary::GetProc(uint32_t ordinal) {
    return nullptr;
}

Battlenet::Library* Battlenet::Library::Load(const char* path, Allocator& allocator) {
    void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (!module) {
        return nullptr;
    }

    void* memory = allocator.Alloc(sizeof(WindowsLibrary));

    if (!memory) {
        dlclose(module);
        return nullptr;
    }

    WindowsLibrary* library = new (memory) WindowsLibrary();
    library->m_module = module;
    library->m_allocator = &allocator;

    return library;
}

Battlenet::WindowsLibrary::~WindowsLibrary() {
    dlclose(this->m_module);
}
