#pragma once

// Engine registries may not be populated when Metamod loads a plugin.
// Cache successful lookups only; rate-limit failures and reset at map boundaries.
template <typename T> class DeferredLookup
{
public:
    template <typename Lookup> T* Resolve(double now, Lookup lookup)
    {
        if (m_value || now < m_nextAttempt) return m_value;
        ++m_attempts;
        m_nextAttempt = now + 1.0;
        m_value = lookup();
        return m_value;
    }

    void Reset() { m_value = nullptr; m_nextAttempt = 0.0; m_attempts = 0; }
    T* Get() const { return m_value; }
    unsigned Attempts() const { return m_attempts; }

private:
    T* m_value = nullptr;
    double m_nextAttempt = 0.0;
    unsigned m_attempts = 0;
};
