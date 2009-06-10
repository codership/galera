#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "galera_state.h"
#include "galerautils.h"

#define STATE_FILE "grastate.dat"
#define STATE_HEADER "# GALERA saved state, version: %f, date: %s"

static int make_state_file_name(const char *dir, char *file_name, int len) {
  if (strlen(dir) + strlen(STATE_FILE) + 1 > (uint)len) {
        gu_error("state file path name too long");
        return -1;
    }
    sprintf(file_name, "%s/%s", dir, STATE_FILE);
    return 0;
}

int galera_store_state(const char *dir, galera_state_t *state) {
    char file_name[256];
    FILE *fid;
    time_t now = time(NULL);

    if (make_state_file_name(dir, file_name, 256)) {
        gu_error("could not create state file: %s, %s", dir, file_name);
        return -1;
    }
    fid = fopen(file_name, "w");
    if (!fid) {
        gu_error("could not open state file: %s", file_name);
        return -1;
    }
    fprintf(fid, STATE_HEADER, 0.6, ctime(&now));
    fprintf(fid, "uuid:               ");
    //fprintf(fid, GU_UUID_FORMAT, GU_UUID_ARGS(&(state->uuid)));
    fprintf(fid, GU_UUID_FORMAT, GU_UUID_ARGS(&(state->uuid)));
    fprintf(fid, "\n");
    fprintf(fid, "seqno:              %10" PRIi64 "\n", state->last_applied_seqno);


    fclose (fid);
    
    return 0;
}

int galera_restore_state(const char *dir, galera_state_t *state) {
    FILE *fid;
    char file_name[256];
    char row[80];
    float version = 0;
    char time_str[64];
    int i = 0;
 
    if (make_state_file_name(dir, file_name, 256)) {
        gu_error("could not create state file: %s, %s", dir, file_name);
        return -1;
    }

    fid = fopen(file_name, "r");
    if (!fid) {
        gu_error("could not open state file: %s", file_name);
        return -1;
    }

    /* read header line */
    if (!fgets((char *)row, 80, fid)) {
        gu_error("empty state file: %s", file_name);
        return -1;
    }

    if (sscanf(row, STATE_HEADER, &version, time_str) != 2) {
        gu_error("unknown header in state file: %s", file_name);
        gu_error("        row: %s", row);
        return -1;
    }

    do {
        char name[64];
        char value[64];

        i++;

        if (fgets((char *)row, 80, fid) && row[0] != '#' && row[0] != '\0') {
            memset(value, '\0', 64);
            memset(name, '\0', 64);
            if (sscanf(row, "%64s %64s", (char *)name, value) != 2) {
                gu_warn("state file has unknown line: %s", row);
                continue;
            }

            if (!strncmp(name, "uuid:", 5)) {
                if (sscanf(
                    value, GU_UUID_FORMAT, GU_UUID_ARGS_REF(&(state->uuid))
                ) != 16) {
                    gu_error("state file has bad uuid: %s", row);
                }
            } else if (!strncmp(name, "seqno:", 6)) {
                if (sscanf(value, "%" PRIi64, &state->last_applied_seqno) != 1) {
                    gu_error("state file has bad seqno: %s", row);
                }
            } else {
                gu_error("Galera: unknown parameter in state file: row: %d %s", 
                         i, name
                );
            }
        } 
        if (ferror(fid)) {
            gu_error("state file read fail: %d", ferror(fid));
        }
    } while(!feof(fid));

    fclose(fid);
    return 0;
}
