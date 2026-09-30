/*
 * sim_core_api.c -- callback table storage, retrieval and readiness check
 */

#include "sim_core_api.h"

static sim_core_callbacks_t g_cb = { NULL, NULL, NULL, NULL };
static int                 g_cb_ready = 0;

void sim_core_set_callbacks(const sim_core_callbacks_t *cb)
{
    if (cb != NULL && cb->malloc != NULL && cb->free != NULL && cb->inject != NULL) {
        g_cb = *cb;
        g_cb_ready = 1;
    }
}

const sim_core_callbacks_t *sim_core_get_callbacks(void)
{
    return &g_cb;
}

int sim_core_callbacks_ready(void)
{
    return g_cb_ready;
}
