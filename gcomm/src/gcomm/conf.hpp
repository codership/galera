/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

/*!
 * @file conf.hpp
 *
 * @brief Configuration parameters and utility templates.
 */

#ifndef GCOMM_CONF_HPP
#define GCOMM_CONF_HPP

#include "gu_config.hpp"
#include "gu_uri.hpp"
#include "gu_throw.hpp"

namespace gcomm
{
    /*!
     * Configuration parameter definitions.
     *
     * Transport definition and configuration parameters are passed to
     * Transport::create() in the URI form. URI scheme part defines
     * which transport is returned. Currently recognized are "tcp", "gmcast"
     * and "pc". This will change in the future.
     *
     * URI format is the following:
     * gcomm://[<peer_host>[:<peer_port>]][?<key1>=<val1>&<key2>=<val2>]...
     * The key/value pairs can be used to pass configuration parameters to
     * gcomm layers.
     *
     * Time periods as parameter values follow ISO8601 duration representation
     * (as represented in http://en.wikipedia.org/wiki/ISO_8601#Durations).
     * Examples:
     *  - PT1S - one second
     *  - PT1M30S = one minute 30 secs
     *  - P1DT6H = one day, 6 hours
     *
     * To get subsecond resolution, second part can be represented as decimal
     * number, but currently it is not recommended due to bug in Period
     * parsing routine (rounding errors can result inaccurate boundary
     * value checking).
     */
    struct Conf
    {
        static std::string const ProtonetBackend;
        static std::string const ProtonetVersion;

        /*!
         * @brief TCP non-blocking flag ("socket.non_blocking")
         *
         * Parameter value is boolean (passed 0 or 1) denoting whether
         * the socket should or should not be in non-blocking state.
         */
        static std::string const TcpNonBlocking;

        /*!
         * @brief Use SSL sockets for communication
         *
         * Boolean describing whether underlying transport should use SSL
         * connections.
         */
        static std::string const SocketUseSsl;

        /*!
         * @brief File containing CA certificates in PEM format
         *        ("socket.ssl_verify_file")
         */
        static std::string const SocketSslVerifyFile;

        /*!
         * @brief File containing certificate to use in PEM format
         *        ("socket.ssl_certificate_file")
         */
        static std::string const SocketSslCertificateFile;

        /*!
         * @brief File containing private key associated with certificate
         *        ("socket.ssl_private_key_file")
         *
         * If private key file is protected with password,
         * SocketSslPasswordFile ("socket.ssl_password_file") must also be set.
         */
        static std::string const SocketSslPrivateKeyFile;

        /*!
         * @brief File containing password used to protect private key file
         *        ("socket.ssl_password_file")
         */
        static std::string const SocketSslPasswordFile;

        /*!
         * @brief Cipher list for SSL connections (socket.ssl_cipher_list)
         */
        static std::string const SocketSslCipherList;

        /*!
         * @brief Disable compression boolean (socket.ssl_disable_compression)
         */
        static std::string const SocketSslCompression;

        /*!
         * @brief Algorithm for message checksums:
         * 0 - none    (backward compatible)
         * 1 - CRC-32  (backward compatible)
         * 2 - CRC-32C (optimized and potentially HW-accelerated on Intel CPUs)
         */
        static std::string const SocketChecksum;

        /*!
         * @brief GMCast scheme for transport URI ("gmcast")
         */
        static std::string const GMCastScheme;

        /*!
         * @brief GMCast protocol version
         */
        static std::string const GMCastVersion;

        /*!
         * @brief GMCast group name ("gmcast.group")
         *
         * String denoting group name. Max length of string is 16. Peer nodes
         * accept GMCast connection only if the group names match.
         */
        static std::string const GMCastGroup;

        /*!
         * @brief GMCast listening address ("gmcast.listen_addr")
         *
         * Listening address for GMCast. Address is currently passed in
         * URI format (for example tcp://192.168.3.1:4567) and it should
         * be passed as the last configuration parameter in order to
         * avoid confusion. If parameter value is undefined, GMCast
         * starts listening all interfaces at default port 4567.
         */
        static std::string const GMCastListenAddr;

        /*!
         * @brief GMCast multicast address ("gmcast.mcast_addr")
         *
         * Multicast address for GMCast. By default multicast socket
         * is bound to the same interface as conf::GMCastListenAddr.
         * If multicast interface must be specified, the only way
         * to do it is currently via listening address configuration.
         */
        static std::string const GMCastMCastAddr;

        /*!
         * @brief GMCast multicast port ("gmcast.mcast_port")
         *
         * Multicast port for GMCast. By default multicast uses the
         * same port as GMCast TCP connections.
         */
        static std::string const GMCastMCastPort;

        /*!
         * @brief GMCast multicast TTL ("gmcast.mcast_ttl")
         *
         * This parameter controls multicast packet TTL. By default it
         * is set to 1 and usually it should not be changed unless
         * adviced so. This means that multicast is limited to single LAN
         * segment.
         */
        static std::string const GMCastMCastTTL;

        static std::string const GMCastTimeWait;
        static std::string const GMCastPeerTimeout;

        /*!
         * @brief Maximum initial reconnect attempts
         *
         * Maximum initial reconnect attempts for address reported by peer.
         */
        static std::string const GMCastMaxInitialReconnectAttempts;

        /*!
         * @brief Add or remove peer address.
         *
         * Setting value to add:<scheme>://<ip>:<port> will inject new peer
         * address in address list. Setting value to del:<scheme>://<ip>:<port>
         * will remove peer address from list (via forget procedure).
         */
        static std::string const GMCastPeerAddr;

        /*!
         * @brief Isolate node from peers
         *
         * Setting this value to 'true' closes all conections
         * and will prevent forming of new connections until
         * value is set again to 'false'. This parameter should be
         * used for testing purposes only and it will not be visible
         * in global configuration array.
         */
        static std::string const GMCastIsolate;

        /*!
         * @brief Segment identifier for segmentation.
         */
        static std::string const GMCastSegment;


        /*!
         * @brief EVS scheme for transport URI ("evs")
         */
        static std::string const EvsScheme;

        /*!
         * @brief EVS protocol version
         */
        static std::string const EvsVersion;

        /*!
         * @brief EVS view forget timeout ("evs.view_forget_timeout")
         *
         * This timeout controls how long information about
         * known group views is maintained. This information is needed
         * to filter out delayed messages from previous views that are not
         * live anymore. Default value is 5 minutes and there is usually not
         * need to change it.
         */
        static std::string const EvsViewForgetTimeout;

        /*!
         * @brief EVS suspect timeout ("evs.suspect_timeout")
         *
         * This timeout controls how long node can remain silent until
         * it is put under suspicion. If majority of the current group
         * agree that the node is under suspicion, it is discarded from
         * group and new group view is formed immediately. If majority
         * of the group does not agree about suspicion, Conf::EvsInactiveTimeout
         * is waited until forming of new group will be attempted.
         * Default value is 5 seconds.
         */
        static std::string const EvsSuspectTimeout;

        /*!
         * @brief EVS inactive timeout ("evs.inactive_timeout")
         *
         * This timeout control how long node can remain completely silent
         * until it is discarded from the group. This is hard limit, unlike
         * Conf::EvsSuspectTimeout, and the node is discarded even if it
         * becomes live during the formation of the new group. Default value
         * is 15 seconds.
         */
        static std::string const EvsInactiveTimeout;

        /*!
         * @brief EVS inactive check period ("evs.inactive_check_period")
         *
         * This period controls how often node liveness is checked. Default
         * is 1 second and there is no need to change this unless
         * Conf::EvsSuspectTimeout or Conf::EvsInactiveTimeout is adjusted
         * to smaller value. Default value is 1 second, minimum is 0.1 seconds
         * and maximum is Conf::EvsSuspectTimeout/2.
         */
        static std::string const EvsInactiveCheckPeriod;


        static std::string const EvsInstallTimeout;

        /*!
         * @brief EVS keepalive period ("evs.keepalive_period")
         *
         * This timeout controls how often keepalive messages are
         * sent into network. Node liveness is determined with
         * these keepalives, so the value sould be significantly smaller
         * than Conf::EvsSuspectTimeout. Default value is 1 second,
         * minimum is 0.1 seconds and maximum is Conf::EvsSuspectTimeout/3.
         */
        static std::string const EvsKeepalivePeriod;

        /*!
         * @brief EVS join retransmission period ("evs.join_retrans_period")
         *
         * This parameter controls how often join messages are retransmitted
         * during group formation. There is usually no need to adjust
         * this value. Default value is 0.3 seconds, minimum is 0.1 seconds
         * and maximum is Conf::EvsSuspectTimeout/3.
         */
        static std::string const EvsJoinRetransPeriod;

        /*!
         * @brief EVS statistics reporting period ("evs.stats_report_period")
         *
         * This parameters controls how often statistics information is
         * printed in the log. This parameter has effect only if
         * statistics reporting is enabled via Conf::EvsInfoLogMask. Default
         * value is 1 minute.
         */
        static std::string const EvsStatsReportPeriod;

        /*!
         * @brief EVS debug log mask ("evs.debug_log_mask")
         *
         * This mask controls what debug information is printed in the logs
         * if debug logging is turned on. Mask value is bitwise-or
         * from values gcomm::evs::Proto::DebugFlags. By default only
         * state information is printed.
         */
        static std::string const EvsDebugLogMask;

        /*!
         * @brief EVS info log mask ("evs.info_log_mask")
         *
         * This mask controls what info log is printed in the logs.
         * Mask value is bitwise-or from values gcomm::evs::Proto::InfoFlags.
         */
        static std::string const EvsInfoLogMask;

        /*!
         * @brief EVS send window ("evs.send_window")
         *
         * This parameter controls how many messages protocol layer is
         * allowed to send without getting all acknowledgements for any of them.
         * Default value is 32.
         */
        static std::string const EvsSendWindow;

        /*!
         * @brief EVS user send window ("evs.user_send_window")
         *
         * Like Conf::EvsSendWindow, but for messages for which sending
         * is initiated by call from upper layer. Default value is 16.
         */
        static std::string const EvsUserSendWindow;

        /*!
         * @brief EVS message aggregation mode ("evs.use_aggregate")
         *
         * This parameter controls whether EVS is allowed to aggregate
         * several user messages into one message. By default this option
         * is enabled and there should be no need to disable it unless
         * adviced so.
         */
        static std::string const EvsUseAggregate;

        /*!
         * @brief Period to generate keepalives for causal messages
         *
         */
        static std::string const EvsCausalKeepalivePeriod;

        /*!
         * @brief EVS maximum install timeouts ("evs.max_install_timeouts")
         *
         * This parameter controls how many install attempts are done
         * before declaring other nodes as inactive and trying to re-establish
         * group via singleton views.
         */
        static std::string const EvsMaxInstallTimeouts;

        /*!
         * @brief PC scheme for transport URI ("pc")
         */
        static std::string const PcScheme;

        /*!
         * @brief PC protocol version
         */
        static std::string const PcVersion;

        /*!
         * @brief PC split-brain mode
         *
         * This parameter controls whether PC is allowed to continue
         * operation despite of possible split brain condition.
         */
        static std::string const PcIgnoreSb;

        /*!
         * @brief PC quorum mode
         *
         * This parameter controls whether PC is allowed to continue
         * operation despite of quorum loss.
         */
        static std::string const PcIgnoreQuorum;

        /*!
         * @brief PC message checksumming
         *
         * This parameter controls whether PC layer does message
         * checksumming.
         */
        static std::string const PcChecksum;

        /*!
         * @brief PC starup announce timeout
         */
        static std::string const PcAnnounceTimeout;

        /*!
         * @brief PC close linger timeout
         */
        static std::string const PcLinger;

        /*!
         * @brief PC newer prim view overrides
         */
        static std::string const PcNpvo;

        /*!
         * @brief If set during runtime bootstraps new PC
         */
        static std::string const PcBootstrap;

        /*!
         * @brief Wait for prim comp unconditionally if set to true
         */
        static std::string const PcWaitPrim;

        /*!
         * @brief Timeout on waiting for primary component
         */
        static std::string const PcWaitPrimTimeout;

        /*!
         * @brief Node weight in prim comp voting
         */
        static std::string const PcWeight;

        static void register_params(gu::Config&);
    };


    // Helper templates to read configuration parameters.

    template <typename T> T _conf_param(const gu::URI& uri,
                                        const std::string& param,
                                        const T* default_value = 0,
                                        const T* min_value = 0,
                                        const T* max_value = 0)
    {
        T ret;
        try
        {
            ret = gu::from_string<T>(uri.get_option(param));
        }
        catch (gu::NotFound& e)
        {
            // cppcheck-suppress nullPointer
            if (default_value == 0)
            {
                gu_throw_error(EINVAL)
                    << "param " << param << " not found from uri "
                    << uri.to_string();
            }
            // cppcheck-suppress nullPointer
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
        return _conf_param(uri, param, &default_value,
                           reinterpret_cast<const T*>(0), &max_value);
    }

    template <typename T> T conf_param_def_range(const gu::URI& uri,
                                                 const std::string& param,
                                                 const T& default_value,
                                                 const T& min_value,
                                                 const T& max_value)
    {
        return _conf_param(uri, param, &default_value, &min_value, &max_value);
    }



    template <typename T>
    T param(gu::Config&        conf,
            const gu::URI&     uri,
            const std::string& key,
            const std::string& def,
            std::ios_base& (*f)(std::ios_base&) = std::dec)
    {
        std::string ret(def);
        ret = conf.get(key, ret);
        return gu::from_string<T>(uri.get_option(key, ret), f);
    }

    template <typename T>
    T check_range(const std::string& key,
                  const T&           val,
                  const T&           min,
                  const T&           max)
    {
        if (val < min || val >= max)
        {
            gu_throw_error(ERANGE) << "param '" << key << "' value " << val
                                   << " out of range [" << min
                                   << "," << max << ")";
        }
        return val;
    }

} // namespace gcomm

#endif // GCOMM_CONF_HPP
