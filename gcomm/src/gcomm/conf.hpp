/**
 * Strings containing config parameter hierarchy and utility 
 * functions to read param values
 */
#ifndef _GCOMM_CONF_HPP_
#define _GCOMM_CONF_HPP_

#include <gcomm/common.hpp>
#include <gcomm/string.hpp>

BEGIN_GCOMM_NAMESPACE

namespace Conf
{
    
    static const string GCommPrefix = "gcomm";
    static const string SchemeDelim = "+";
    static const string QueryDelim = "&";
    static const string ParamKeyDelim = ".";
    
    static const string EvsPrefix = "evs";
    static const string EvsScheme = GCommPrefix + SchemeDelim + EvsPrefix;
    /* Timeout in milliseconds to wait joining */
    static const string EvsQueryJoinWait = EvsPrefix + ParamKeyDelim + "join_wait";

    static const string VsPrefix = "vs";
    static const string VsScheme = GCommPrefix + SchemeDelim + VsPrefix;

    static const string PcPrefix = "pc";
    static const string PcScheme = GCommPrefix + SchemeDelim + PcPrefix;
    
    /* 
     * Transport parameters 
     */

    /* 
     * TCP 
     */
    
    /* Prefix */
    static const string TcpPrefix = "tcp";
    /* Scheme */
    static const string TcpScheme = GCommPrefix + SchemeDelim + TcpPrefix;
    /* Parameter denoting asynchronous/non-blocking mode */
    static const string TcpParamNonBlocking = TcpPrefix + ParamKeyDelim + "non_blocking";
    static const string TcpParamMaxPending = TcpPrefix + ParamKeyDelim + "max_pending";
    
    /* */
    static const string TipcPrefix = "tipc";
    static const string TipcScheme = GCommPrefix + SchemeDelim + TipcPrefix;
    
    static const string GMCastPrefix = "gmcast";
    static const string GMCastScheme = GCommPrefix + SchemeDelim + GMCastPrefix;
    
    static const string GMCastQueryNode = GMCastPrefix + ParamKeyDelim + "node";
    static const string GMCastQueryGroup = GMCastPrefix + ParamKeyDelim + "group";
    
    static const string NodePrefix = "node";
    static const string NodeQueryName = NodePrefix + ParamKeyDelim + "name";

    static const string DummyPrefix = "dummy";
    static const string DummyScheme = GCommPrefix + SchemeDelim + DummyPrefix;
    


    
}


END_GCOMM_NAMESPACE


#endif // _GCOMM_CONF_HPP_
