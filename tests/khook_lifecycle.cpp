// Exercise the real Metamod KHook C++ wrapper with a minimal host dispatcher.
// This verifies registration/callback filtering, not CS2's engine ABI.
#include <cassert>
#include <memory>
#include <vector>
#include <khook.hpp>
#include "utils/khook_helpers.h"

namespace KHook { IKHook* __exported__khook = nullptr; }

std::vector<CKHookBase*>& GetKHookList()
{
    static std::vector<CKHookBase*> hooks;
    return hooks;
}

struct Engine
{
    virtual void GameFrame(bool, bool, bool) {}
};

class Host final : public KHook::IKHook
{
public:
    void* context = nullptr;
    void* post = nullptr;
    int registrations = 0;
    int removals = 0;

    KHook::HookID_t SetupHook(void*, void*, void*, void*, void*, void*, void*, unsigned int, bool) override
    { assert(false); return KHook::INVALID_HOOK; }
    KHook::HookID_t SetupVirtualHook(void**, int index, void* ctx, void*, void*, void* callback, void*, void*, unsigned int, bool) override
    {
        assert(index == 0);
        context = ctx;
        post = callback;
        return ++registrations;
    }
    void RemoveHook(KHook::HookID_t, bool, void (*)(KHook::HookID_t, void*), void*) override
    { ++removals; post = nullptr; context = nullptr; }
    void* GetContextPtr() override { return context; }
    void* GetOriginalFunction() override { return nullptr; }
    void* GetOriginalValuePtr() override { return nullptr; }
    void* GetOverrideValuePtr() override { return nullptr; }
    void* GetCurrentValuePtr(bool) override { return nullptr; }
    void DestroyReturnValue() override {}
    void* FindOriginal(void*) override { return nullptr; }
    void* FindOriginalVirtual(void**, int) override { return nullptr; }
    void* DoRecall(KHook::Action, void*, std::size_t, void*, void*) override { return nullptr; }
    void SaveReturnValue(KHook::Action action, void*, std::size_t, void*, void*, bool) override
    { assert(action == KHook::Action::Ignore); }
    void* LookupSignature(void*, std::size_t, const char*) override { return nullptr; }
    bool WasOriginalFunctionSkipped() override { return false; }

    void Frame(Engine& engine)
    {
        if (post)
        {
            auto callback = KHook::BuildMFP<void (Engine::*)(bool, bool, bool)>(post);
            (engine.*callback)(true, true, true);
        }
    }
};

static int frames = 0;
static KHook::Return<void> OnFrame(Engine*, bool, bool, bool)
{
    ++frames;
    return {KHook::Action::Ignore};
}

int main()
{
    Host host;
    KHook::__exported__khook = &host;
    Engine engine, unrelated;
    Engine* instance = nullptr;
    {
        auto hook = MakeKHookVirtual(&Engine::GameFrame, instance, nullptr, OnFrame);
        hook->Add(); // Interfaces are not acquired yet.
        assert(host.registrations == 0);
        instance = &engine;
        host.Frame(engine);
        assert(frames == 0); // Constructor alone must not be mistaken for attachment.
        for (auto* entry : GetKHookList()) entry->Add();
        hook->Add(); // Idempotent.
        assert(host.registrations == 1);
        host.Frame(engine);
        assert(frames == 1);
        host.Frame(unrelated);
        assert(frames == 1);
        for (auto* entry : GetKHookList()) entry->Remove();
        host.Frame(engine);
        assert(frames == 1);
        hook->Add();
        host.Frame(engine);
        assert(frames == 2);
    }
    assert(host.removals == 1);
    host.Frame(engine);
    assert(frames == 2);
    GetKHookList().clear();
}
