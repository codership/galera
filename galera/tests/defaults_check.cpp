//
// Copyright (C) 2018-2020 Codership Oy <info@codership.com>
//

#include <wsrep_api.h>
extern "C" int wsrep_loader(wsrep_t*);

#include <gu_config.hpp>
#include <gu_mutex.hpp>
#include <gu_cond.hpp>
#include <gu_lock.hpp>

#include <gu_arch.h> // GU_WORDSIZE

#include <map>
#include <string>
#include <sstream>
#include <iostream>

#include <check.h>

#include <unistd.h> // unlink()

/* "magic" value for parameters which defaults a variable */
static const char* const VARIABLE = "variable";

static const char* Defaults[] =
{
    "base_dir",                    ".",
    "base_port",                   "4567",
    "cert.log_conflicts",          "no",
    "cert.optimistic_pa",          "yes",
    "debug",                       "no",
#ifdef GU_DBUG_ON
    "dbug",                        "",
#endif
    "evs.auto_evict",              "0",
    "evs.causal_keepalive_period", "PT1S",
    "evs.debug_log_mask",          "0x1",
    "evs.delay_margin",            "PT1S",
    "evs.delayed_keep_period",     "PT30S",
    "evs.inactive_check_period",   "PT0.5S",
    "evs.inactive_timeout",        "PT15S",
    "evs.info_log_mask",           "0",
    "evs.install_timeout",         "PT7.5S",
    "evs.join_retrans_period",     "PT1S",
    "evs.keepalive_period",        "PT1S",
    "evs.max_install_timeouts",    "3",
    "evs.send_window",             "4",
    "evs.stats_report_period",     "PT1M",
    "evs.suspect_timeout",         "PT5S",
    "evs.use_aggregate",           "true",
    "evs.user_send_window",        "2",
    "evs.version",                 "1",
    "evs.view_forget_timeout",     "P1D",
#ifndef NDEBUG
    "gcache.debug",                "0",
#endif
    "gcache.dir",                  ".",
    "gcache.keep_pages_size",      "0",
    "gcache.mem_size",             "0",
    "gcache.name",                 "galera.cache",
    "gcache.page_size",            "128M",
    "gcache.recover",              "yes",
    "gcache.size",                 "128M",
    "gcomm.thread_prio",           "",
    "gcs.fc_debug",                "0",
    "gcs.fc_factor",               "1.0",
    "gcs.fc_limit",                "16",
    "gcs.fc_master_slave",         "no",
    "gcs.max_packet_size",         "64500",
    "gcs.max_throttle",            "0.25",
#if (GU_WORDSIZE == 32)
    "gcs.recv_q_hard_limit",       "2147483647",
#elif (GU_WORDSIZE == 64)
    "gcs.recv_q_hard_limit",       "9223372036854775807",
#endif
    "gcs.recv_q_soft_limit",       "0.25",
    "gcs.sync_donor",              "no",
    "gmcast.listen_addr",          "tcp://0.0.0.0:4567",
    "gmcast.mcast_addr",           "",
    "gmcast.mcast_ttl",            "1",
    "gmcast.peer_timeout",         "PT3S",
    "gmcast.segment",              "0",
    "gmcast.time_wait",            "PT5S",
    "gmcast.version",              "0",
//  "ist.recv_addr",               no default,
    "pc.announce_timeout",         "PT3S",
    "pc.checksum",                 "false",
    "pc.ignore_quorum",            "false",
    "pc.ignore_sb",                "false",
    "pc.linger",                   "PT20S",
    "pc.npvo",                     "false",
    "pc.recovery",                 "true",
    "pc.version",                  "0",
    "pc.wait_prim",                "true",
    "pc.wait_prim_timeout",        "PT30S",
    "pc.weight",                   "1",
    "protonet.backend",            "asio",
    "protonet.version",            "0",
    "repl.causal_read_timeout",    "PT30S",
    "repl.commit_order",           "3",
    "repl.key_format",             "FLAT8",
    "repl.max_ws_size",            "2147483647",
    "repl.proto_max",              "10",
#ifdef GU_DBUG_ON
    "signal",                      "",
#endif
    "socket.checksum",             "2",
    "socket.recv_buf_size",        "auto",
    "socket.send_buf_size",        "auto",
//  "socket.ssl",                  no default,
//  "socket.ssl_cert",             no default,
//  "socket.ssl_cipher",           no default,
//  "socket.ssl_compression",      no default,
//  "socket.ssl_key",              no default,
    NULL
};

typedef std::map<std::string, std::string> DefaultsMap;

static void
fill_in_expected(DefaultsMap& map, const char* def_list[])
{
    for (int i(0); def_list[i] != NULL; i += 2)
    {
        std::pair<std::string, std::string> param(def_list[i], def_list[i+1]);
        DefaultsMap::iterator it(map.insert(param).first);

        ck_assert_msg(it != map.end(), "Failed to insert KV pair: %s = %s",
                      param.first.c_str(), param.second.c_str());
    }
}

static void
fill_in_real(DefaultsMap& map, wsrep_t& provider)
{
    std::vector<std::pair<std::string, std::string> > kv_pairs;
    char* const opt_string(provider.options_get(&provider));
    gu::Config::parse(kv_pairs, opt_string);
    ::free(opt_string);

    for (unsigned int i(0); i < kv_pairs.size(); ++i)
    {
        std::pair<std::string, std::string> const trimmed(kv_pairs[i].first,
                                                          kv_pairs[i].second);
        DefaultsMap::iterator it(map.insert(trimmed).first);

        ck_assert_msg(it != map.end(), "Failed to insert KV pair: %s = %s",
                      trimmed.first.c_str(), trimmed.second.c_str());
    }
}

static void
log_cb(wsrep_log_level_t l, const char* c)
{
    if (l <= WSREP_LOG_ERROR) // only log errors to avoid output clutter
    {
        std::cerr << c << '\n';
    }
}

struct app_ctx
{
    gu::Mutex mtx_;
    gu::Cond  cond_;
    wsrep_t   provider_;
    bool      connected_;

    app_ctx() : mtx_(), cond_(), provider_(), connected_(false) {}
};

static enum wsrep_cb_status
conn_cb(void* ctx, const wsrep_view_info_t* view)
{
    (void)view;
    app_ctx* c(static_cast<app_ctx*>(ctx));
    gu::Lock lock(c->mtx_);

    if (!c->connected_)
    {
        c->connected_ = true;
        c->cond_.broadcast();
    }
    else
    {
        assert(0);
    }

    return WSREP_CB_SUCCESS;
}

static enum wsrep_cb_status
view_cb(void*                    app_ctx,
        void*                    recv_ctx,
        const wsrep_view_info_t* view,
        const char*              state,
        size_t                   state_len)
{
    /* make compilers happy about unused arguments */
    (void)app_ctx;
    (void)recv_ctx;
    (void)view;
    (void)state;
    (void)state_len;

    return WSREP_CB_SUCCESS;
}

static enum wsrep_cb_status
synced_cb(void* app_ctx)
{
    (void)app_ctx;

    return WSREP_CB_SUCCESS;
}

static void*
recv_func(void* ctx)
{
    app_ctx* c(static_cast<app_ctx*>(ctx));
    wsrep_t& provider(c->provider_);

    wsrep_status_t const ret(provider.recv(&provider, NULL));
    ck_assert_msg(WSREP_OK == ret, "recv() returned %d", ret);

    return NULL;
}

START_TEST(defaults)
{
    DefaultsMap expected_defaults, real_defaults;

    fill_in_expected(expected_defaults, Defaults);

    app_ctx ctx;
    wsrep_t& provider(ctx.provider_);
    int ret = wsrep_status_t(wsrep_loader(&provider));
    ck_assert(WSREP_OK == ret);

    struct wsrep_init_args init_args =
        {
            &ctx, // void* app_ctx

            /* Configuration parameters */
            NULL, // const char* node_name
            NULL, // const char* node_address
            NULL, // const char* node_incoming
            NULL, // const char* data_dir
            NULL, // const char* options
            0,    // int         proto_ver

            /* Application initial state information. */
            NULL, // const wsrep_gtid_t* state_id
            NULL, // const wsrep_buf_t*  state

            /* Application callbacks */
            log_cb, // wsrep_log_cb_t         logger_cb
            conn_cb,// wsrep_connected_cb_t   connected_cb
            view_cb,// wsrep_view_cb_t        view_handler_cb
            NULL,   // wsrep_sst_request_cb_t sst_request_cb
            NULL,   // wsrep_encrypt_cb_t     encrypt_cb

            /* Applier callbacks */
            NULL, // wsrep_apply_cb_t      apply_cb
            NULL, // wsrep_unordered_cb_t  unordered_cb

            /* State Snapshot Transfer callbacks */
            NULL, // wsrep_sst_donate_cb_t sst_donate_cb
            synced_cb,// wsrep_synced_cb_t synced_cb
        };
    ret = provider.init(&provider, &init_args);
    ck_assert(WSREP_OK == ret);

    /* some defaults are set only on connection attmept */
    ret = provider.connect(&provider, "cluster_name", "gcomm://", "", false);
    ck_assert_msg(WSREP_OK == ret, "connect() returned %d", ret);

    fill_in_real(real_defaults, provider);
    mark_point();

    if (WSREP_OK == ret) /* if connect() was a success, need to disconnect() */
    {
        /* some configuration change events need to be received */
        gu_thread_t recv_thd;
        gu_thread_create(&recv_thd, NULL, recv_func, &ctx);

        mark_point();

        /* @todo:there is a race condition in the library when disconnect() is
         * called right after connect() */
        { /* sync with connect callback */
            gu::Lock lock(ctx.mtx_);
            while(!ctx.connected_) lock.wait(ctx.cond_);
        }

        mark_point();

        ret = provider.disconnect(&provider);
        ck_assert_msg(WSREP_OK == ret, "disconnect() returned %d", ret);

        ret = gu_thread_join(recv_thd, NULL);
        ck_assert_msg(0 == ret, "Could not join thread: %d (%s)",
                      ret, strerror(ret));
    }

    provider.free(&provider);
    mark_point();

    /* cleanup files */
    ::unlink(real_defaults.find("gcache.name")->second.c_str());
    ::unlink("grastate.dat");

    /* now compare expected and real maps */
    std::ostringstream err;
    DefaultsMap::iterator expected(expected_defaults.begin());
    while (expected != expected_defaults.end())
    {
        DefaultsMap::iterator real(real_defaults.find(expected->first));

        if (real != real_defaults.end())
        {
            if (expected->second != VARIABLE &&
                expected->second != real->second)
            {
                err << "Provider default for " << real->first
                    << ": " << real->second << " differs from expected "
                    << expected->second << '\n';
            }

            real_defaults.erase(real);
        }
        else
        {
            err << "Provider missing " << expected->first <<" parameter\n";
        }

        expected_defaults.erase(expected++);
    }

    mark_point();

    DefaultsMap::iterator real(real_defaults.begin());
    while (real != real_defaults.end())
    {
        err << "Provider has extra parameter: " << real->first << " = "
            << real->second << '\n';
        real_defaults.erase(real++);
    }

    ck_assert_msg(err.str().empty(), "Defaults discrepancies detected:\n%s",
                  err.str().c_str());
}
END_TEST

Suite* defaults_suite()
{
    Suite* s = suite_create("Defaults");
    TCase* tc;

    tc = tcase_create("defaults");
    tcase_add_test(tc, defaults);
    tcase_set_timeout(tc, 120);
    suite_add_tcase(s, tc);

    return s;
}

