
#include "galera.h"


#include <dlfcn.h>
#include <errno.h>
#include <assert.h>

int galera_load(const char *spec, galera_t **hptr)
{
    int ret = 0;
    void *dlh = NULL;
    galera_loader_fun dlfun;

    if (!(spec && hptr))
        return EINVAL;
    *hptr = NULL;
    
    if (!(dlh = dlopen(spec, RTLD_LOCAL))) {
        ret = errno;
        goto out;
    }
    
    if (!(*(void **)(&dlfun) = dlsym(dlh, "galera_loader"))) {
        ret = errno;
        goto out;
        
    }

    ret = (*dlfun)(hptr);
    
    /* What would be the most appropriate errno? */
    if (ret == 0 && !*hptr)
        ret = EACCES;
    
out:
    if (!*hptr)
        dlclose(dlh);
    else
        (*hptr)->dlh = dlh;
    return ret;
}



void galera_unload(galera_t *hptr)
{
    void *dlh;
    assert(hptr);
    dlh = hptr->dlh;
    hptr->tear_down(hptr);
    if (dlh)
        dlclose(dlh);
}

