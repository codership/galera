// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
//#include <galerautils.h>

#include "../gcs_backend.h"
#include "gcs_backend_test.h"

enum
{
    DUMMY_BACKEND,
    SPREAD_BACKEND,
    VS_BACKEND,
    ILL_BACKEND
};

static long opened = ILL_BACKEND;

GCS_BACKEND_NAME_FN(gcs_dummy_name)
{
    return "DUMMIEEEE!";
}

GCS_BACKEND_OPEN_FN(gcs_dummy_open)
{
    backend->name = gcs_dummy_name;
    opened = DUMMY_BACKEND;
    return 0;
}

GCS_BACKEND_OPEN_FN(gcs_spread_open)
{
    opened = SPREAD_BACKEND;
    return 0;
}

GCS_BACKEND_OPEN_FN(gcs_vs_open)
{
    opened = VS_BACKEND;
    return 0;
}

START_TEST (gcs_backend_test)
{
    gcs_backend_t backend;
    long ret;

    fail_if (opened != ILL_BACKEND);

    ret = gcs_backend_init (&backend, NULL, "wrong://kkk");
    fail_if (ret != -ESOCKTNOSUPPORT);
    fail_if (opened != ILL_BACKEND);

    ret = gcs_backend_init (&backend, NULL, "spread:");
    fail_if (ret != -EINVAL);
    fail_if (opened != ILL_BACKEND);

    ret = gcs_backend_init (&backend, NULL, "dummy://");
    fail_if (ret != 0, "ret = %d (%s)", ret, strerror(-ret));
    fail_if (backend.name != gcs_dummy_name);
    fail_if (opened != DUMMY_BACKEND);

    ret = gcs_backend_init (&backend, NULL, "gcomm://kkk");
    fail_if (ret != 0, "ret = %d (%s)", ret, strerror(-ret));
    fail_if (opened != VS_BACKEND);

    ret = gcs_backend_init (&backend, NULL, "spread://");
    fail_if (ret != 0, "ret = %d (%s)", ret, strerror(-ret));
    fail_if (opened != SPREAD_BACKEND);

}
END_TEST

Suite *gcs_backend_suite(void)
{
    Suite *suite = suite_create("GCS backend interface");
    TCase *tcase = tcase_create("gcs_backend");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_backend_test);
    return suite;
}

