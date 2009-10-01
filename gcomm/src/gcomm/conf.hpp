/**
 * Strings containing config parameter hierarchy and utility 
 * functions to read param values
 */
#ifndef _GCOMM_CONF_HPP_
#define _GCOMM_CONF_HPP_

#include <gcomm/common.hpp>

namespace gcomm
{

    namespace Conf
    {
    
        static const std::string GCommPrefix   = "gcomm";
        static const std::string SchemeDelim   = "+";
        static const std::string QueryDelim    = "&";
        static const std::string ParamKeyDelim = ".";
    
        static const std::string EvsPrefix = "evs";
        static const std::string EvsScheme = GCommPrefix + SchemeDelim + EvsPrefix;
        /* Timeout in milliseconds to wait joining */
        static const std::string EvsQueryJoinWait = EvsPrefix + ParamKeyDelim + "join_wait";

        static const std::string VsPrefix = "vs";
        static const std::string VsScheme = GCommPrefix + SchemeDelim + VsPrefix;

        static const std::string PcPrefix = "pc";
        static const std::string PcScheme = GCommPrefix + SchemeDelim + PcPrefix;
    
        /* 
         * Transport parameters 
         */

        /* 
         * TCP 
         */
    
        /* Prefix */
        static const std::string TcpPrefix = "tcp";
        /* Scheme */
        static const std::string TcpScheme = GCommPrefix + SchemeDelim + TcpPrefix;
        /* Parameter denoting asynchronous/non-blocking mode */
        static const std::string TcpParamNonBlocking =
            TcpPrefix + ParamKeyDelim + "non_blocking";
        static const std::string TcpParamMaxPending =
            TcpPrefix + ParamKeyDelim + "max_pending";
    
        /* */
        static const std::string TipcPrefix = "tipc";
        static const std::string TipcScheme = GCommPrefix + SchemeDelim + TipcPrefix;
    
        static const std::string GMCastPrefix = "gmcast";
        static const std::string GMCastScheme = GCommPrefix + SchemeDelim + GMCastPrefix;
    
        // static const std::string GMCastQueryNode =
        //    GMCastPrefix + ParamKeyDelim + "node";
        static const std::string GMCastQueryGroup =
            GMCastPrefix + ParamKeyDelim + "group";
        static const std::string GMCastQueryListenAddr =
            GMCastPrefix + ParamKeyDelim + "listen_addr";
    
        static const std::string NodePrefix    = "node";
        static const std::string NodeQueryName = NodePrefix + ParamKeyDelim + "name";

        static const std::string DummyPrefix = "dummy";
        static const std::string DummyScheme = GCommPrefix + SchemeDelim + DummyPrefix;
    
    } // namespace Conf

} // namespace gcomm

#endif // _GCOMM_CONF_HPP_
