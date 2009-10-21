#ifndef _GCOMM_COMMON_HPP_
#define _GCOMM_COMMON_HPP_

#define BEGIN_GCOMM_NAMESPACE namespace gcomm {
#define END_GCOMM_NAMESPACE }

namespace gcomm
{
    class DeleteObjectOp
    {
    public:
        template <typename T> void operator()(T* t) { delete t; }
    };
}

#endif // _GCOMM_COMMON_HPP_
