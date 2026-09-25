#include <cassert>
#include "utils/deferred_lookup.h"

int main()
{
    DeferredLookup<int> resolver;
    int descriptor = 350;
    int calls = 0;
    bool registered = false;
    auto registry = [&]() -> int* { ++calls; return registered ? &descriptor : nullptr; };

    assert(resolver.Get() == nullptr && calls == 0); // No lookup during construction/load.
    assert(resolver.Resolve(10.0, registry) == nullptr);
    registered = true; // The engine registers messages after plugin loading.
    assert(resolver.Resolve(10.1, registry) == nullptr && calls == 1);
    assert(resolver.Resolve(11.0, registry) == &descriptor && calls == 2);
    assert(resolver.Resolve(12.0, registry) == &descriptor && calls == 2);
    resolver.Reset(); // Do not retain a descriptor from the previous map.
    registered = false;
    for (int time = 0; time < 20; ++time)
        assert(resolver.Resolve(time, registry) == nullptr);
    assert(resolver.Attempts() == 20);
    registered = true; // Continue retrying even after a prolonged absence.
    assert(resolver.Resolve(20.0, registry) == &descriptor);
    assert(resolver.Attempts() == 21);
}
