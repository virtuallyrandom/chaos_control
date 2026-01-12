namespace cc
{
    ScopeElapsed::ScopeElapsed(steady_clock::duration* const d)
        : m_duration(d),
        m_start(steady_clock::now())
    {
    }

    ScopeElapsed::ScopeElapsed()
    {
    }

    ScopeElapsed::~ScopeElapsed()
    {
        if (m_duration != nullptr)
            *m_duration = current();
    }

    steady_clock::duration ScopeElapsed::current() const
    {
        return steady_clock::now() - m_start;
    }

} // namespace cc
