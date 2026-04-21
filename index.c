#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int object_write(ObjectType, const void *, size_t, ObjectID *);

int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];

        unsigned int mode;
        char hex[65];
        unsigned long long mtime;
        unsigned int size;

        if (fscanf(f, "%o %64s %llu %u %511s",
                   &mode, hex, &mtime, &size, e->path) < 5)
            break;

        e->mode = mode;
        e->mtime_sec = mtime;
        e->size = size;

        hex_to_hash(hex, &e->hash);
        index->count++;
    }

    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    FILE *f = fopen(".pes/index", "w");
    if (!f) return -1;

    char hex[65];

    for (int i = 0; i < index->count; i++) {
        hash_to_hex(&index->entries[i].hash, hex);

        fprintf(f, "%o %s %llu %u %s\n",
                index->entries[i].mode,
                hex,
                (unsigned long long)index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fclose(f);
    return 0;
}

int index_add(Index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    ObjectID id;
    object_write(OBJ_BLOB, data, size, &id);
    free(data);

    struct stat st;
    stat(path, &st);

    IndexEntry *e = &index->entries[index->count++];
    strcpy(e->path, path);
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;
    e->mode = 0100644;

    return index_save(index);
}

int index_status(const Index *index) {
    printf("Staged changes:\n");

    if (index->count == 0) {
        printf("    (nothing to show)\n");
        return 0;
    }

    for (int i = 0; i < index->count; i++)
        printf("    staged: %s\n", index->entries[i].path);

    return 0;
}
