#include "commit.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

int object_write(ObjectType, const void *, size_t, ObjectID *);
int object_read(const ObjectID *, ObjectType *, void **, size_t *);
int tree_from_index(ObjectID *);
int hex_to_hash(const char *, ObjectID *);
void hash_to_hex(const ObjectID *, char *);

// ─── commit_parse ─────────────────────────────────────────────────────

int commit_parse(const void *data, size_t len, Commit *c) {
    const char *p = (const char *)data;
    char hex[65];

    // tree
    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    hex_to_hash(hex, &c->tree);

    p = strchr(p, '\n');
    if (!p) return -1;
    p++;

    // parent (optional)
    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        hex_to_hash(hex, &c->parent);
        c->has_parent = 1;

        p = strchr(p, '\n');
        if (!p) return -1;
        p++;
    } else {
        c->has_parent = 0;
    }

    // author
    char author_line[256];
    if (sscanf(p, "author %255[^\n]\n", author_line) != 1) return -1;

    char *space = strrchr(author_line, ' ');
    if (!space) return -1;

    c->timestamp = strtoull(space + 1, NULL, 10);
    *space = '\0';

    strncpy(c->author, author_line, sizeof(c->author) - 1);
    c->author[sizeof(c->author) - 1] = '\0';

    // skip author, committer, blank line
    for (int i = 0; i < 3; i++) {
        p = strchr(p, '\n');
        if (!p) return -1;
        p++;
    }

    // message
    strncpy(c->message, p, sizeof(c->message) - 1);
    c->message[sizeof(c->message) - 1] = '\0';

    return 0;
}

// ─── commit_serialize ────────────────────────────────────────────────

int commit_serialize(const Commit *c, void **out, size_t *len) {
    char buf[4096];
    char tree_hex[65], parent_hex[65];

    hash_to_hex(&c->tree, tree_hex);

    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "tree %s\n", tree_hex);

    if (c->has_parent) {
        hash_to_hex(&c->parent, parent_hex);
        n += snprintf(buf + n, sizeof(buf) - n, "parent %s\n", parent_hex);
    }

    n += snprintf(buf + n, sizeof(buf) - n,
        "author %s %" PRIu64 "\n"
        "committer %s %" PRIu64 "\n"
        "\n"
        "%s",
        c->author, c->timestamp,
        c->author, c->timestamp,
        c->message
    );

    *out = malloc(n);
    if (!*out) return -1;

    memcpy(*out, buf, n);
    *len = n;
    return 0;
}

// ─── head_read ───────────────────────────────────────────────────────

int head_read(ObjectID *id) {
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) return -1;

    char line[256];

    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (strncmp(line, "ref:", 4) == 0) {
        char path[256];
        sscanf(line, "ref: %s", path);

        char full[256];
        snprintf(full, sizeof(full), ".pes/%s", path);

        f = fopen(full, "r");
        if (!f) return -1;

        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return -1;
        }
        fclose(f);
    }

    line[strcspn(line, "\n")] = 0;
    return hex_to_hash(line, id);
}

// ─── head_update ─────────────────────────────────────────────────────

int head_update(const ObjectID *id) {
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) return -1;

    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    char path[256];

    if (strncmp(line, "ref:", 4) == 0) {
        sscanf(line, "ref: %s", path);

        char full[256];
        snprintf(full, sizeof(full), ".pes/%s", path);

        f = fopen(full, "w");
    } else {
        f = fopen(".pes/HEAD", "w");
    }

    if (!f) return -1;

    char hex[65];
    hash_to_hex(id, hex);

    fprintf(f, "%s\n", hex);
    fclose(f);

    return 0;
}

// ─── commit_walk ─────────────────────────────────────────────────────

int commit_walk(commit_walk_fn fn, void *ctx) {
    ObjectID id;

    if (head_read(&id) != 0) return -1;

    while (1) {
        ObjectType type;
        void *data;
        size_t len;

        if (object_read(&id, &type, &data, &len) != 0)
            return -1;

        Commit c;
        if (commit_parse(data, len, &c) != 0) {
            free(data);
            return -1;
        }
        free(data);

        fn(&id, &c, ctx);

        if (!c.has_parent) break;

        id = c.parent;
    }

    return 0;
}

// ─── commit_create ───────────────────────────────────────────────────

int commit_create(const char *msg, ObjectID *out) {
    ObjectID tree;

    if (tree_from_index(&tree) != 0) {
        fprintf(stderr, "error: nothing staged\n");
        return -1;
    }

    Commit c;
    memset(&c, 0, sizeof(c));

    c.tree = tree;

    if (head_read(&c.parent) == 0)
        c.has_parent = 1;

    const char *author = getenv("USER");
    if (!author) author = "unknown";

    snprintf(c.author, sizeof(c.author), "%s", author);
    c.timestamp = (uint64_t)time(NULL);
    snprintf(c.message, sizeof(c.message), "%s", msg);

    void *data;
    size_t len;

    if (commit_serialize(&c, &data, &len) != 0)
        return -1;

    if (object_write(OBJ_COMMIT, data, len, out) != 0) {
        free(data);
        return -1;
    }

    free(data);

    return head_update(out);
}
