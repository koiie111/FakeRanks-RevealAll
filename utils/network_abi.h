#pragma once
#include <cstddef>
#include <type_traits>
#include <networksystem/inetworkserializer.h>

// 1.41.9.7 removed the per-message vtable. Building with the old SDK crashes
// on both descriptor lookup and allocation, even when all signatures match.
static_assert(!std::is_polymorphic_v<INetworkMessageInternal>, "Use the CS2 1.41.9.7+ network-message SDK");
static_assert(sizeof(INetworkMessageInternal) == 40, "Unexpected network descriptor size");
static_assert(offsetof(NetMessageInfo_t, m_pfnAllocate) == 8, "Unexpected message allocator offset");
static_assert(offsetof(NetMessageInfo_t, m_MessageId) == 24, "Unexpected message ID offset");
