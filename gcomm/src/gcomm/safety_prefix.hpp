#ifndef SAFETY_PREFIX
#define SAFETY_PREFIX

namespace gcomm
{
    enum SafetyPrefix
    {
        SP_UNRELIABLE = 0,
        SP_FIFO       = 1,
        SP_AGREED     = 2,
        SP_SAFE       = 3
    };
}

#endif // SAFETY_PREFIX
