
#include <galera.h>

#include <errno.h>
#include <string.h>

typedef struct dg_ {
    int foo;
} dg_t;

#define PRIV(_p) ((dg_t *)(_p)->opaque);


void tear_down(galera_t *hptr)
{
    dg_t *dg = PRIV(hptr);
    free(dg);
    free(hptr);
}




int galera_loader(galera_t **hptr)
{
    if (!hptr)
        return EINVAL;
    
    *hptr = malloc(sizeof(galera_t));
    if (!*hptr)
        return ENOMEM;
    
    memset(*hptr, 0, sizeof(galera_t));
    
    (*hptr)->tear_down = &tear_down;
    return 0;
}
