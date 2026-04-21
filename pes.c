#include "pes.h"
#include "index.h"
#include "commit.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

void cmd_init(void) {
    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);
    mkdir(".pes/refs", 0755);
    mkdir(".pes/refs/heads", 0755);

    FILE *f = fopen(".pes/HEAD", "w");
    fprintf(f, "ref: refs/heads/main\n");
    fclose(f);

    printf("Initialized empty PES repository in .pes/\n");
}

void cmd_add(int argc, char *argv[]) {
    Index index;
    index_load(&index);

    for (int i = 2; i < argc; i++) {
        index_add(&index, argv[i]);
        printf("added: %s\n", argv[i]);
    }
}

void cmd_status(void) {
    Index index;
    index_load(&index);
    index_status(&index);
}

void cmd_commit(int argc, char *argv[]) {
    const char *msg = NULL;

    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-m") == 0)
            msg = argv[i + 1];
    }

    if (!msg) {
        printf("error: commit message required\n");
        return;
    }

    ObjectID id;
    if (commit_create(msg, &id) != 0) {
        printf("error: commit failed\n");
        return;
    }

    char hex[65];
    hash_to_hex(&id, hex);
    printf("Committed: %.12s... %s\n", hex, msg);
}

static void log_cb(const ObjectID *id, const Commit *c, void *ctx) {
    char hex[65];
    hash_to_hex(id, hex);

    printf("commit %s\n", hex);
    printf("Author: %s\n", c->author);
    printf("Date:   %" PRIu64 "\n\n", c->timestamp);
    printf("    %s\n\n", c->message);
}

void cmd_log(void) {
    if (commit_walk(log_cb, NULL) != 0)
        printf("No commits\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    if (!strcmp(argv[1], "init")) cmd_init();
    else if (!strcmp(argv[1], "add")) cmd_add(argc, argv);
    else if (!strcmp(argv[1], "status")) cmd_status();
    else if (!strcmp(argv[1], "commit")) cmd_commit(argc, argv);
    else if (!strcmp(argv[1], "log")) cmd_log();

    return 0;
}
