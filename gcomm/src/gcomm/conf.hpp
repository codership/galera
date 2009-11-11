/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


/**
 * Strings containing config parameter hierarchy and utility 
 * functions to read param values
 */
#ifndef _GCOMM_CONF_HPP_
#define _GCOMM_CONF_HPP_

#include "gu_uri.hpp"
#include "gu_throw.hpp"

namespace gcomm
{
    
    namespace Conf
    {
    
        static const std::string GCommPrefix   = "";
        static const std::string SchemeDelim   = "";
        static const std::string QueryDelim    = "&";
        static const std::string ParamKeyDelim = ".";
        
        static const std::string EvsPrefix = "evs";
        static const std::string EvsScheme = GCommPrefix + SchemeDelim + EvsPrefix;
        /* Timeout in milliseconds to wait joining */
        static const std::string EvsQueryJoinWait = EvsPrefix + ParamKeyDelim + "join_wait";
        
        static const std::string EvsParamViewForgetTimeout = 
            EvsPrefix + ParamKeyDelim + "view_forget_timeout";
        static const std::string EvsParamInactiveTimeout = 
            EvsPrefix + ParamKeyDelim + "inactive_timeout";
        static const std::string EvsParamInactiveCheckPeriod = 
            EvsPrefix + ParamKeyDelim + "inactive_check_period";
        static const std::string EvsParamConsensusTimeout = 
            EvsPrefix + ParamKeyDelim + "consensus_timeout";
        static const std::string EvsParamRetransPeriod = 
            EvsPrefix + ParamKeyDelim + "retrans_period";
        static const std::string EvsParamJoinRetransPeriod =
            EvsPrefix + ParamKeyDelim + "join_retrans_period";
        static const std::string EvsParamStatsReportPeriod =
            EvsPrefix + ParamKeyDelim + "stats_report_period";
        static const std::string EvsParamDebugLogMask = 
            EvsPrefix + ParamKeyDelim + "debug_log_mask";
        static const std::string EvsParamInfoLogMask =
            EvsPrefix + ParamKeyDelim + "info_log_mask";
        

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
            "socket" + ParamKeyDelim + "non_blocking";
        static const std::string TcpParamMaxPending =
            "socket" + ParamKeyDelim + "max_pending";
        
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
    
    
    template <typename T> T _conf_param(const gu::URI& uri, 
                                        const std::string& param,
                                        const T* default_value = 0,
                                        const T* min_value = 0,
                                        const T* max_value = 0) 
        throw (gu::Exception)
    {
        T ret;
        try
        {
            ret = gu::from_string<T>(uri.get_option(param));
        }
        catch (gu::NotFound& e)
        {
            if (default_value == 0)
            {
                gu_throw_error(EINVAL) 
                    << "param " << param << " not found from uri " 
                    << uri.to_string();
            }
            ret = *default_value;
        }
        
        if (min_value != 0 && *min_value > ret)
        {
            gu_throw_error(EINVAL)
                << "param " << param << " value " << ret << " out of range "
                << "min allowed " << *min_value;
        }
        
        if (max_value != 0 && *max_value < ret)
        {
            gu_throw_error(EINVAL)
                << "param " << param << " value " << ret << " out of range "
                << "max allowed " << *max_value;
        }
        return ret;
    }

    template <typename T> T conf_param(const gu::URI& uri,
                                       const std::string& param)
    {
        return _conf_param<T>(uri, param, 0, 0, 0);
    }
    
    template <typename T> T conf_param_def(const gu::URI& uri,
                                           const std::string& param,
                                           const T& default_value)
    {
        return _conf_param(uri, param, &default_value);
    }
    
    template <typename T> T conf_param_range(const gu::URI& uri,
                                             const std::string& param,
                                             const T& min_value,
                                             const T& max_value)
    {
        return _conf_param(uri, param, 0, &min_value, &max_value);
    }

    template <typename T> T conf_param_def_min(const gu::URI& uri,
                                               const std::string& param,
                                               const T& default_value,
                                               const T& min_value)

    {
        return _conf_param(uri, param, &default_value, &min_value);
    }

    template <typename T> T conf_param_def_max(const gu::URI& uri,
                                               const std::string& param,
                                               const T& default_value,
                                               const T& max_value)

    {
        return _conf_param(uri, param, &default_value, 0, &max_value);
    }
    
    template <typename T> T conf_param_def_range(const gu::URI& uri,
                                                 const std::string& param,
                                                 const T& default_value,
                                                 const T& min_value,
                                                 const T& max_value)
    {
        return _conf_param(uri, param, &default_value, &min_value, &max_value);
    }
    
} // namespace gcomm

#endif // _GCOMM_CONF_HPP_
