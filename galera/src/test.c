
#include <galera.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int err;
    galera_t *g = NULL;

    if (argc != 2) {
        fprintf(stderr, "Usage: test <filename>\n");
        return EXIT_FAILURE;
    }
    


    if ((err = galera_load(argv[1], &g)) != 0) {
        fprintf(stderr, "Failed to load '%s': '%s'\n", 
                argv[1], strerror(err));
    } else {
        fprintf(stderr, "Library loaded successfully\n");
        galera_unload(g);
    }
    
    return EXIT_SUCCESS;
}
