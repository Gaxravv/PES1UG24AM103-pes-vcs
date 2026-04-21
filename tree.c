#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int object_write(ObjectType, const void *, size_t, ObjectID *);

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;

    const unsigned char *p = data;
    const unsigned char *end = p + len;

    while (p < end) {
        TreeEntry *e = &tree_out->entries[tree_out->count];

        sscanf((char *)p, "%o %s", &e->mode, e->name);

        while (*p) p++;
        p++;

        memcpy(e->hash.hash, p, HASH_SIZE);
        p += HASH_SIZE;

        tree_out->count++;
    }

    return 0;
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    unsigned char *buf = malloc(4096);
    size_t off = 0;

    for (int i = 0; i < tree->count; i++) {
        off += sprintf((char *)buf + off, "%o %s", tree->entries[i].mode, tree->entries[i].name) + 1;
        memcpy(buf + off, tree->entries[i].hash.hash, HASH_SIZE);
        off += HASH_SIZE;
    }

    *data_out = buf;
    *len_out = off;
    return 0;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    index_load(&index);

    if (index.count == 0) return -1;

    Tree tree;
    tree.count = index.count;

    for (int i = 0; i < index.count; i++) {
        strcpy(tree.entries[i].name, index.entries[i].path);
        tree.entries[i].mode = index.entries[i].mode;
        tree.entries[i].hash = index.entries[i].hash;
    }

    void *data;
    size_t len;
    tree_serialize(&tree, &data, &len);

    int r = object_write(OBJ_TREE, data, len, id_out);
    free(data);

    return r;
}
