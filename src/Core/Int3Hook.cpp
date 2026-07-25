#include "pch.h"
#include "Int3Hook.h"
#include "Logger.h"
#include <cstring>

static constexpr int MAX_HOOKS = 4;

struct HookEntry {
    void* target = nullptr;
    void* handler = nullptr;
    uint8_t origByte = 0;
    bool active = false;
};

static HookEntry s_hooks[MAX_HOOKS] = {};
static void* s_vehHandle = nullptr;

static HookEntry* FindEntry(void* target)
{
    for (auto& e : s_hooks)
        if (e.target == target) return &e;
    return nullptr;
}

static HookEntry* FindFreeSlot()
{
    for (auto& e : s_hooks)
        if (!e.active) return &e;
    return nullptr;
}

static LONG CALLBACK VEHHandler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

    void* addr = ep->ExceptionRecord->ExceptionAddress;
    auto* entry = FindEntry(addr);
    if (!entry) return EXCEPTION_CONTINUE_SEARCH;

    DWORD old = 0;
    VirtualProtect(entry->target, 1, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t*)entry->target = entry->origByte;
    VirtualProtect(entry->target, 1, old, &old);

    ep->ContextRecord->Rip = (uint64_t)entry->handler;
    return EXCEPTION_CONTINUE_EXECUTION;
}

bool Int3Hook::Install(void* target, void* handler)
{
    if (!target || !handler) return false;

    auto* entry = FindFreeSlot();
    if (!entry) {
        LOG("Int3Hook", "No free slot for %llX", (uint64_t)target);
        return false;
    }

    if (!s_vehHandle) {
        s_vehHandle = AddVectoredExceptionHandler(1, VEHHandler);
        if (!s_vehHandle) {
            LOG_MSG("Int3Hook", "AddVectoredExceptionHandler failed");
            return false;
        }
    }

    entry->target = target;
    entry->handler = handler;
    entry->origByte = *(uint8_t*)target;
    entry->active = true;

    DWORD old = 0;
    VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t*)target = 0xCC;
    VirtualProtect(target, 1, old, &old);

    LOG("Int3Hook", "Installed at %llX (orig=%02X handler=%llX)",
        (uint64_t)target, entry->origByte, (uint64_t)handler);
    return true;
}

void Int3Hook::Uninstall(void* target)
{
    auto* entry = FindEntry(target);
    if (!entry || !entry->active) return;

    DWORD old = 0;
    VirtualProtect(entry->target, 1, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t*)entry->target = entry->origByte;
    VirtualProtect(entry->target, 1, old, &old);

    entry->target = nullptr;
    entry->handler = nullptr;
    entry->origByte = 0;
    entry->active = false;
}

void Int3Hook::ReArm(void* target)
{
    auto* entry = FindEntry(target);
    if (!entry || !entry->active) return;

    DWORD old = 0;
    VirtualProtect(entry->target, 1, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t*)entry->target = 0xCC;
    VirtualProtect(entry->target, 1, old, &old);
}
