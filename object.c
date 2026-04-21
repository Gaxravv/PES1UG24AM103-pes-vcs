// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions: object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int digest_len = HASH_SIZE;
    EVP_Digest(data, len, id_out->hash, &digest_len, EVP_sha256(), NULL);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // Step 1: Determine type string
    const char *type_str;
    if (type == OBJ_BLOB)        type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // Step 2: Build header "blob 16\0" — note the +1 to include the null byte
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;

    // Step 3: Combine header + data into one buffer
    size_t full_len = (size_t)header_len + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;
    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // Step 4: Compute SHA-256 of the entire object
    compute_hash(full_obj, full_len, id_out);

    // Step 5: Deduplication — if already stored, nothing to do
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // Step 6: Create shard directory (.pes/objects/XX/)
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir, 0755);

    // Step 7: Get final object path and build temp path in the same directory
    char path[512];
    object_path(id_out, path, sizeof(path));
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/%.2s/tmp_%s", OBJECTS_DIR, hex, hex + 2);

    // Step 8: Write to temp file
    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full_obj); return -1; }

    ssize_t written = write(fd, full_obj, full_len);
    free(full_obj);
    if (written != (ssize_t)full_len) { close(fd); return -1; }

    // Step 9: fsync to flush to disk
    fsync(fd);
    close(fd);

    // Step 10: Atomic rename temp -> final
    if (rename(tmp_path, path) != 0) return -1;

    // Step 11: fsync the directory to persist the rename
    int dir_fd = open(dir, O_RDONLY);
    if (dir_fd >= 0) { fsync(dir_fd); close(dir_fd); }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // Step 1: Get the file path
    char path[512];
    object_path(id, path, sizeof(path));

    // Step 2: Open and read entire file into memory
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(file_size);
    if (!buf) { fclose(f); return -1; }

    if (fread(buf, 1, file_size, f) != file_size) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    // Step 3: Integrity check — recompute hash and compare to filename
    ObjectID computed;
    compute_hash(buf, file_size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf); return -1;
    }

    // Step 4: Find the null byte separating header from data
    uint8_t *null_pos = memchr(buf, '\0', file_size);
    if (!null_pos) { free(buf); return -1; }

    // Step 5: Parse type from header
    if (strncmp((char *)buf, "blob ", 5) == 0)        *type_out = OBJ_BLOB;
    else if (strncmp((char *)buf, "tree ", 5) == 0)   *type_out = OBJ_TREE;
    else if (strncmp((char *)buf, "commit ", 7) == 0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }

    // Step 6: Copy data portion (everything after the null byte)
    uint8_t *data_start = null_pos + 1;
    *len_out = file_size - (size_t)(data_start - buf);
    *data_out = malloc(*len_out + 1);
    if (!*data_out) { free(buf); return -1; }
    memcpy(*data_out, data_start, *len_out);

    free(buf);
    return 0;
}
