
#include <wsrep.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int err;
    // galera_t *g = NULL;

    if (argc != 2) {
        fprintf(stderr, "Usage: test <filename>\n");
        return EXIT_FAILURE;
    }
    


    if ((err = wsrep_load(argv[1], NULL)) != 0) {
        fprintf(stderr, "Failed to load '%s': '%s'\n", 
                argv[1], strerror(err));
        return EXIT_FAILURE;
    } else {
        fprintf(stderr, "Library loaded successfully\n");
        wsrep_unload(NULL);
    }

    return EXIT_SUCCESS;
}
