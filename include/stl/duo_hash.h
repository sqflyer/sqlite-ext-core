// Copyright 2020 Joshua J Baker. All rights reserved.
// Modifications Copyright 2026 DuoSTL Authors.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

/**
 * @file duo_hash.h
 * @brief High-performance Robin Hood Key-Value Hash Map with backward-shift deletion.
 *
 * DuoSTL hash map implementation derived and evolved from tidwall/hashmap.c.
 * Features:
 * - Separate Key and Value storage and independent destructors (Issue #38).
 * - Safe in-place atomic predicate filtering with automatic table shrinking (Issue #41).
 * - Const-correct query semantics, safe direct copy-out, and bucket probing (Issue #42).
 * - Capacity doubling arithmetic overflow hardening (Issue #48).
 * - Unaligned loads and integer shift undefined behavior hardening in Murmur3 (Issue #49).
 * - Pluggable memory allocator architecture (DUO_MALLOC / sqlite3_malloc64 / custom).
 * - Standalone hashing algorithms: xxHash3 (default), SipHash-2-4, MurmurHash3_86_128.
 */

#ifndef DUO_HASH_H
#define DUO_HASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "duo_alloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DUO_HASHMAP_GROW_AT   0.60 /* 60% */
#define DUO_HASHMAP_SHRINK_AT 0.10 /* 10% */

#ifndef DUO_HASHMAP_LOAD_FACTOR
#define DUO_HASHMAP_LOAD_FACTOR DUO_HASHMAP_GROW_AT
#endif
#ifndef HASHMAP_LOAD_FACTOR
#define HASHMAP_LOAD_FACTOR DUO_HASHMAP_LOAD_FACTOR
#endif



/**
 * @brief Packed 8-byte bucket header storing 48-bit hash signature and 16-bit DIB.
 */
struct duo_hash_bucket {
    uint64_t hash : 48;
    uint64_t dib  : 16;
};
typedef struct duo_hash_bucket duo_hash_bucket_t;

// duo_hashmap is an open-addressed Key-Value hash table using Robin Hood hashing
// with backward-shift deletion and dedicated key and value destructors.
struct duo_hashmap {
    size_t key_size;
    size_t val_size;
    size_t key_offset;
    size_t val_offset;
    size_t bucketsz;
    size_t cap;
    uint64_t seed0;
    uint64_t seed1;
    uint64_t (*hash)(const void *key, uint64_t seed0, uint64_t seed1);
    int (*compare)(const void *key_a, const void *key_b, void *udata);
    void (*key_free)(void *key);
    void (*val_free)(void *val);
    void *udata;
    size_t nbuckets;
    size_t count;
    size_t mask;
    size_t growat;
    size_t shrinkat;
    uint8_t loadfactor;
    uint8_t growpower;
    bool oom;
    void *buckets;
    void *spare;
    void *edata;
};
typedef struct duo_hashmap duo_hashmap_t;
typedef struct duo_hashmap duo_hashset_t;

//-----------------------------------------------------------------------------
// Hashing Algorithms: SipHash-2-4, Murmur3, xxHash3 (duo_hash_xxhash3 defined in duo_alloc.h)
//-----------------------------------------------------------------------------
static inline uint64_t duo_hash_sip(const void *data, size_t len, uint64_t seed0, uint64_t seed1);
static inline uint64_t duo_hash_murmur(const void *data, size_t len, uint64_t seed0, uint64_t seed1);

/**
 * @brief Default 64-bit hash function using xxHash3.
 *
 * @param key   Pointer to the key to hash.
 * @param seed0 First 64-bit seed.
 * @param seed1 Second 64-bit seed.
 * @return 64-bit hash value.
 */
static inline uint64_t duo_hashmap_default_hash(const void *key, uint64_t seed0, uint64_t seed1) {
    (void)seed1;
    // Handled dynamically via key_size inside duo_hashmap_calc_hash
    return duo_hash_xxhash3(key, 8, seed0, seed1);
}

/**
 * @brief Default key comparison callback (falls back to memcmp in compare_keys).
 *
 * @param a     First key pointer.
 * @param b     Second key pointer.
 * @param udata Optional user data pointer.
 * @return 0 if equal, non-zero otherwise.
 */
static inline int duo_hashmap_default_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    (void)a;
    (void)b;
    return 0; // Handled dynamically via memcmp
}

/**
 * @brief Sets the geometric power-of-two growth exponent for capacity resizing.
 *
 * @param map   Pointer to the hash map.
 * @param power Power-of-two multiplier exponent (clamped between 1 and 16; default 1 = double).
 */
static inline void duo_hashmap_set_grow_by_power(duo_hashmap_t *map, size_t power) {
    map->growpower = (uint8_t)(power < 1 ? 1 : power > 16 ? 16 : power);
}

/**
 * @brief Clamps a desired load factor between 0.50 and 0.95.
 *
 * @param factor         Desired load factor floating-point value.
 * @param default_factor Fallback value if factor is NaN.
 * @return Clamped load factor between 0.50 and 0.95.
 */
static inline double duo_hashmap_clamp_load_factor(double factor, double default_factor) {
    return factor != factor ? default_factor : 
           factor < 0.50 ? 0.50 : 
           factor > 0.95 ? 0.95 : 
           factor;
}

/**
 * @brief Configures the target load factor triggering capacity growth.
 *
 * @param map    Pointer to the hash map.
 * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
 */
static inline void duo_hashmap_set_load_factor(duo_hashmap_t *map, double factor) {
    factor = duo_hashmap_clamp_load_factor(factor, map->loadfactor / 100.0);
    map->loadfactor = (uint8_t)(factor * 100);
    map->growat = (size_t)(map->nbuckets * (map->loadfactor / 100.0));
}

/**
 * @brief Computes a direct pointer to the bucket at index.
 *
 * @param map   Pointer to the hash map.
 * @param index Bucket index (0 <= index < nbuckets).
 * @return Pointer to struct duo_hash_bucket.
 */
static inline struct duo_hash_bucket *duo_hashmap_bucket_at(const duo_hashmap_t *map, size_t index) {
    return (struct duo_hash_bucket*)(((char*)map->buckets) + (map->bucketsz * index));
}

/**
 * @brief Retrieves a mutable pointer to the key stored in a bucket.
 *
 * @param map Pointer to the hash map.
 * @param b   Pointer to the bucket.
 * @return Pointer to key payload.
 */
static inline void *duo_hashmap_bucket_key(const duo_hashmap_t *map, struct duo_hash_bucket *b) {
    return (void*)(((char*)b) + map->key_offset);
}

/**
 * @brief Retrieves an immutable pointer to the key stored in a bucket.
 *
 * @param map Pointer to the hash map.
 * @param b   Pointer to the bucket.
 * @return Const pointer to key payload.
 */
static inline const void *duo_hashmap_bucket_key_const(const duo_hashmap_t *map, const struct duo_hash_bucket *b) {
    return (const void*)(((const char*)b) + map->key_offset);
}

/**
 * @brief Retrieves a mutable pointer to the value stored in a bucket.
 *
 * @param map Pointer to the hash map.
 * @param b   Pointer to the bucket.
 * @return Pointer to value payload.
 */
static inline void *duo_hashmap_bucket_val(const duo_hashmap_t *map, struct duo_hash_bucket *b) {
    return (void*)(((char*)b) + map->val_offset);
}

/**
 * @brief Retrieves an immutable pointer to the value stored in a bucket.
 *
 * @param map Pointer to the hash map.
 * @param b   Pointer to the bucket.
 * @return Const pointer to value payload.
 */
static inline const void *duo_hashmap_bucket_val_const(const duo_hashmap_t *map, const struct duo_hash_bucket *b) {
    return (const void*)(((const char*)b) + map->val_offset);
}

/**
 * @brief Masks a 64-bit hash to 48 bits for packed bucket storage.
 *
 * @param hash Full 64-bit hash value.
 * @return Truncated 48-bit hash value.
 */
static inline uint64_t duo_hashmap_clip_hash(uint64_t hash) {
    return hash & 0xFFFFFFFFFFFFULL;
}

/**
 * @brief Computes the clipped 48-bit hash for a given key.
 *
 * Invokes the map's custom hash callback if provided; otherwise hashes map->key_size bytes via xxHash3.
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to the key.
 * @return Clipped 48-bit hash.
 */
static inline uint64_t duo_hashmap_calc_hash(const duo_hashmap_t *map, const void *key) {
    if (map->hash) {
        return duo_hashmap_clip_hash(map->hash(key, map->seed0, map->seed1));
    }
    return duo_hashmap_clip_hash(duo_hash_xxhash3(key, map->key_size, map->seed0, map->seed1));
}

/**
 * @brief Compares two keys for equality.
 *
 * Invokes map->compare if provided; otherwise falls back to binary memcmp over map->key_size bytes.
 *
 * @param map   Pointer to the hash map.
 * @param key_a First key pointer.
 * @param key_b Second key pointer.
 * @return 0 if keys match, non-zero otherwise.
 */
static inline int duo_hashmap_compare_keys(const duo_hashmap_t *map, const void *key_a, const void *key_b) {
    if (map->compare) {
        return map->compare(key_a, key_b, map->udata);
    }
    return memcmp(key_a, key_b, map->key_size);
}

/**
 * @brief Allocates and initializes a new Key-Value hash map using DuoSTL allocators (DUO_MALLOC / DUO_FREE).
 *
 * Hardened against capacity doubling arithmetic overflow (Issue #48).
 *
 * @param key_size  Size of each key in bytes (must be > 0).
 * @param val_size  Size of each value in bytes (0 for set-like behavior).
 * @param cap       Minimum initial bucket capacity (will round up to power of 2, min 16).
 * @param seed0     First 64-bit seed passed to hash function.
 * @param seed1     Second 64-bit seed passed to hash function.
 * @param hash      Custom hash callback (or NULL for default xxHash3).
 * @param compare   Custom key comparison callback (or NULL for memcmp).
 * @param key_free  Callback to destroy key resources on delete/clear/free (Issue #38).
 * @param val_free  Callback to destroy value resources on overwrite/delete/clear/free (Issue #38).
 * @param udata     User data pointer forwarded to compare callback.
 * @return Pointer to newly allocated duo_hashmap_t, or NULL on OOM / invalid arguments.
 */
static inline duo_hashmap_t *duo_hashmap_new(
    size_t key_size, 
    size_t val_size, 
    size_t cap, 
    uint64_t seed0, 
    uint64_t seed1,
    uint64_t (*hash)(const void *key, uint64_t seed0, uint64_t seed1),
    int (*compare)(const void *key_a, const void *key_b, void *udata),
    void (*key_free)(void *key),
    void (*val_free)(void *val),
    void *udata)
{
    if (key_size == 0) {
        return NULL;
    }

    size_t ncap = 16;
    if (cap < ncap) {
        cap = ncap;
    } else {
        while (ncap < cap) {
            if (ncap > SIZE_MAX / 2) return NULL;
            ncap *= 2;
        }
        cap = ncap;
    }

    size_t key_offset = sizeof(struct duo_hash_bucket);
    size_t val_offset = key_offset + key_size;
    while (val_offset & (sizeof(uintptr_t) - 1)) {
        val_offset++;
    }

    size_t bucketsz = val_offset + (val_size > 0 ? val_size : 0);
    while (bucketsz & (sizeof(uintptr_t) - 1)) {
        bucketsz++;
    }

    if (bucketsz > SIZE_MAX / 4) {
        return NULL;
    }

    size_t size = sizeof(struct duo_hashmap) + bucketsz * 2;
    duo_hashmap_t *map = (duo_hashmap_t*)DUO_MALLOC(size);
    if (!map) {
        return NULL;
    }
    DUO_MEMSET(map, 0, sizeof(struct duo_hashmap));
    map->key_size = key_size;
    map->val_size = val_size;
    map->key_offset = key_offset;
    map->val_offset = val_offset;
    map->bucketsz = bucketsz;
    map->seed0 = seed0;
    map->seed1 = seed1;
    map->hash = hash;
    map->compare = compare;
    map->key_free = key_free;
    map->val_free = val_free;
    map->udata = udata;
    map->spare = ((char*)map) + sizeof(struct duo_hashmap);
    map->edata = (char*)map->spare + bucketsz;
    map->cap = cap;
    map->nbuckets = cap;
    map->mask = map->nbuckets - 1;

    map->buckets = DUO_MALLOC(map->bucketsz * map->nbuckets);
    if (!map->buckets) {
        DUO_FREE(map);
        return NULL;
    }
    DUO_MEMSET(map->buckets, 0, map->bucketsz * map->nbuckets);
    map->growpower = 1;
    map->loadfactor = (uint8_t)(duo_hashmap_clamp_load_factor(DUO_HASHMAP_LOAD_FACTOR, DUO_HASHMAP_GROW_AT) * 100);
    map->growat = (size_t)(map->nbuckets * (map->loadfactor / 100.0));
    map->shrinkat = (size_t)(map->nbuckets * DUO_HASHMAP_SHRINK_AT);
    return map;  
}


/**
 * @brief Destroys all active key and value elements by calling key_free and val_free.
 *
 * @param map Pointer to the hash map.
 */
static inline void duo_hashmap_free_elements(duo_hashmap_t *map) {
    if (map->key_free || map->val_free) {
        for (size_t i = 0; i < map->nbuckets; i++) {
            struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
            if (bucket->dib) {
                if (map->key_free) {
                    map->key_free(duo_hashmap_bucket_key(map, bucket));
                }
                if (map->val_free && map->val_size > 0) {
                    map->val_free(duo_hashmap_bucket_val(map, bucket));
                }
            }
        }
    }
}

/**
 * @brief Clears all entries from the map, calling key_free and val_free on every item.
 *
 * @param map        Pointer to the hash map.
 * @param update_cap If true, keeps current allocated bucket count; if false, resets to initial capacity.
 */
static inline void duo_hashmap_clear(duo_hashmap_t *map, bool update_cap) {
    duo_hashmap_free_elements(map);
    map->count = 0;
    if (update_cap) {
        map->cap = map->nbuckets;
    } else if (map->nbuckets != map->cap) {
        void *new_buckets = DUO_MALLOC(map->bucketsz * map->cap);
        if (new_buckets) {
            DUO_FREE(map->buckets);
            map->buckets = new_buckets;
            map->nbuckets = map->cap;
        }
    }
    DUO_MEMSET(map->buckets, 0, map->bucketsz * map->nbuckets);
    map->mask = map->nbuckets - 1;
    map->growat = (size_t)(map->nbuckets * (map->loadfactor / 100.0));
    map->shrinkat = (size_t)(map->nbuckets * DUO_HASHMAP_SHRINK_AT);
}

/**
 * @brief Internal rehash function that reallocates the table and re-inserts all active items.
 *
 * @param map     Pointer to the hash map.
 * @param new_cap New bucket capacity (must be power of two).
 * @return true on success, false on allocation failure.
 */
static inline bool duo_hashmap_resize0(duo_hashmap_t *map, size_t new_cap) {
    duo_hashmap_t *map2 = duo_hashmap_new(map->key_size, map->val_size, new_cap, 
        map->seed0, map->seed1, map->hash, map->compare, map->key_free, map->val_free, map->udata);
    if (!map2) return false;

    for (size_t i = 0; i < map->nbuckets; i++) {
        struct duo_hash_bucket *entry = duo_hashmap_bucket_at(map, i);
        if (!entry->dib) {
            continue;
        }
        entry->dib = 1;
        size_t j = entry->hash & map2->mask;
        while (1) {
            struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map2, j);
            if (bucket->dib == 0) {
                DUO_MEMCPY(bucket, entry, map->bucketsz);
                break;
            }
            if (bucket->dib < entry->dib) {
                DUO_MEMCPY(map2->spare, bucket, map->bucketsz);
                DUO_MEMCPY(bucket, entry, map->bucketsz);
                DUO_MEMCPY(entry, map2->spare, map->bucketsz);
            }
            j = (j + 1) & map2->mask;
            entry->dib += 1;
        }
    }
    DUO_FREE(map->buckets);
    map->buckets = map2->buckets;
    map->nbuckets = map2->nbuckets;
    map->mask = map2->mask;
    map->growat = map2->growat;
    map->shrinkat = map2->shrinkat;
    DUO_FREE(map2);
    return true;
}

/**
 * @brief Resizes the hash map to a new power-of-two capacity.
 *
 * @param map     Pointer to the hash map.
 * @param new_cap Target power-of-two capacity.
 * @return true on success, false on OOM.
 */
static inline bool duo_hashmap_resize(duo_hashmap_t *map, size_t new_cap) {
    return duo_hashmap_resize0(map, new_cap);
}

/**
 * @brief Inserts or updates a Key-Value pair using a precomputed 64-bit hash.
 *
 * Employs Robin Hood hashing with Distance-to-Initial-Bucket (DIB) displacement.
 * On key collision / overwrite:
 * - If val_free is configured, it is called on the old value before the new value is copied in.
 *
 * @param map   Pointer to the hash map.
 * @param key   Pointer to the key bytes to insert.
 * @param val   Pointer to the value bytes (or NULL if val_size is 0).
 * @param hash  Precomputed 64-bit hash of the key.
 * @return true on successful insertion/update, false on allocation failure (OOM).
 */
static inline bool duo_hashmap_set_with_hash(duo_hashmap_t *map, const void *key, 
    const void *val, uint64_t hash)
{
    hash = duo_hashmap_clip_hash(hash);
    map->oom = false;
    if (map->count >= map->growat) {
        if (!duo_hashmap_resize(map, map->nbuckets * (((size_t)1) << map->growpower))) {
            map->oom = true;
            return false;
        }
    }

    struct duo_hash_bucket *entry = (struct duo_hash_bucket*)map->edata;
    entry->hash = hash;
    entry->dib = 1;
    DUO_MEMCPY(duo_hashmap_bucket_key(map, entry), key, map->key_size);
    if (map->val_size > 0 && val) {
        DUO_MEMCPY(duo_hashmap_bucket_val(map, entry), val, map->val_size);
    }

    size_t i = entry->hash & map->mask;
    while (1) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (bucket->dib == 0) {
            DUO_MEMCPY(bucket, entry, map->bucketsz);
            map->count++;
            return true;
        }

        void *b_key = duo_hashmap_bucket_key(map, bucket);
        if (entry->hash == bucket->hash && duo_hashmap_compare_keys(map, key, b_key) == 0) {
            // Overwrite existing key: destroy old value first (Issue #38)
            if (map->val_free && map->val_size > 0) {
                map->val_free(duo_hashmap_bucket_val(map, bucket));
            }
            if (map->val_size > 0 && val) {
                DUO_MEMCPY(duo_hashmap_bucket_val(map, bucket), val, map->val_size);
            }
            return true;
        }

        if (bucket->dib < entry->dib) {
            DUO_MEMCPY(map->spare, bucket, map->bucketsz);
            DUO_MEMCPY(bucket, entry, map->bucketsz);
            DUO_MEMCPY(entry, map->spare, map->bucketsz);
        }
        i = (i + 1) & map->mask;
        entry->dib += 1;
    }
}

/**
 * @brief Inserts or updates a Key-Value pair.
 *
 * Automatically hashes key via map->hash (or xxHash3).
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to key bytes.
 * @param val Pointer to value bytes.
 * @return true on success, false on OOM.
 */
static inline bool duo_hashmap_set(duo_hashmap_t *map, const void *key, const void *val) {
    return duo_hashmap_set_with_hash(map, key, val, duo_hashmap_calc_hash(map, key));
}

/**
 * @brief Retrieves a mutable pointer to the value matching key using a precomputed hash.
 *
 * @param map  Pointer to the hash map.
 * @param key  Pointer to key to search for.
 * @param hash Precomputed 64-bit hash.
 * @return Mutable pointer to stored value, or NULL if not found.
 */
static inline void *duo_hashmap_get_with_hash(const duo_hashmap_t *map, const void *key, uint64_t hash) {
    hash = duo_hashmap_clip_hash(hash);
    size_t i = hash & map->mask;
    while (1) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (!bucket->dib) return NULL;
        if (bucket->hash == hash) {
            void *b_key = duo_hashmap_bucket_key(map, bucket);
            if (duo_hashmap_compare_keys(map, key, b_key) == 0) {
                return map->val_size > 0 ? duo_hashmap_bucket_val(map, bucket) : b_key;
            }
        }
        i = (i + 1) & map->mask;
    }
}

/**
 * @brief Retrieves a mutable pointer to the value associated with key.
 *
 * Note: Pointers into internal bucket storage can be invalidated if subsequent insertions cause a rehash.
 * For safe, rehash-proof retrieval, consider using duo_hashmap_get_copy.
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to key to search for.
 * @return Mutable pointer to value, or NULL if not found.
 */
static inline void *duo_hashmap_get(const duo_hashmap_t *map, const void *key) {
    return duo_hashmap_get_with_hash(map, key, duo_hashmap_calc_hash(map, key));
}

//-----------------------------------------------------------------------------
// Issue #42: Const Access, Safe Copy-Out & Bucket Probing
// In tidwall/hashmap.c Issue #42 ("some functions now returning const void *"),
// return types were changed to 'const void*' to discourage modifying underlying
// bucket data and holding raw internal pointers across table rehashes/reallocations.
// DuoSTL provides a complete, robust solution:
// 1. duo_hashmap_get: returns mutable pointer to value for safe in-place updates.
// 2. duo_hashmap_get_const: returns immutable pointer for const-correct read-only access.
// 3. duo_hashmap_get_copy: copies the value out directly into caller memory,
//    eliminating dangling pointer lifetime bugs across subsequent table rehashes.
// 4. duo_hashmap_probe / duo_hashmap_probe_const: bucket slot probing modulo capacity.
//-----------------------------------------------------------------------------

/**
 * @brief Retrieves an immutable read-only pointer to the value associated with key.
 *
 * Addresses tidwall/hashmap.c Issue #42 for const-correct query semantics.
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to key to search for.
 * @return Const pointer to value, or NULL if not found.
 */
static inline const void *duo_hashmap_get_const(const duo_hashmap_t *map, const void *key) {
    return (const void*)duo_hashmap_get(map, key);
}

/**
 * @brief Copies the value associated with key directly into caller-allocated memory.
 *
 * Addresses tidwall/hashmap.c Issue #42: protects against dangling pointer invalidation
 * caused by subsequent table rehashes, insertions, or deletions.
 *
 * @param map     Pointer to the hash map.
 * @param key     Pointer to key to search for.
 * @param out_val Destination buffer to copy value bytes into (must be at least map->val_size bytes).
 * @return true if key was found and copied, false if key was not found.
 */
static inline bool duo_hashmap_get_copy(const duo_hashmap_t *map, const void *key, void *out_val) {
    const void *v = duo_hashmap_get_const(map, key);
    if (!v) {
        return false;
    }
    if (out_val && map->val_size > 0) {
        DUO_MEMCPY(out_val, v, map->val_size);
    }
    return true;
}

/**
 * @brief Returns a mutable pointer to the value at bucket index modulo capacity.
 *
 * Useful for custom table inspections and external diagnostics.
 *
 * @param map   Pointer to the hash map.
 * @param index Bucket index (modulo table capacity).
 * @return Pointer to value at slot, or NULL if bucket is empty.
 */
static inline void *duo_hashmap_probe(const duo_hashmap_t *map, size_t index) {
    size_t i = index & map->mask;
    struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
    if (!bucket->dib) {
        return NULL;
    }
    return map->val_size > 0 ? duo_hashmap_bucket_val(map, bucket) : duo_hashmap_bucket_key(map, bucket);
}

/**
 * @brief Returns an immutable read-only pointer to the value at bucket index modulo capacity.
 *
 * Addresses tidwall/hashmap.c Issue #42 for const-correct bucket probing.
 *
 * @param map   Pointer to the hash map.
 * @param index Bucket index (modulo table capacity).
 * @return Const pointer to value at slot, or NULL if bucket is empty.
 */
static inline const void *duo_hashmap_probe_const(const duo_hashmap_t *map, size_t index) {
    return (const void*)duo_hashmap_probe(map, index);
}

/**
 * @brief Tests whether key exists in the hash map.
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to key.
 * @return true if key exists, false otherwise.
 */
static inline bool duo_hashmap_contains(const duo_hashmap_t *map, const void *key) {
    return duo_hashmap_get(map, key) != NULL;
}

/**
 * @brief Removes an entry matching key using a precomputed hash and backward-shift deletion.
 *
 * Invokes key_free on the key and val_free on the value (Issue #38).
 * Subsequent colliding buckets are shifted backward to maintain compact probe sequences
 * without creating tombstones.
 * If active count drops below shrink threshold (DUO_HASHMAP_SHRINK_AT = 10%), capacity is halved.
 *
 * @param map  Pointer to the hash map.
 * @param key  Pointer to key to delete.
 * @param hash Precomputed 64-bit hash.
 * @return true if key was found and deleted, false if not found.
 */
static inline bool duo_hashmap_delete_with_hash(duo_hashmap_t *map, const void *key, uint64_t hash) {
    hash = duo_hashmap_clip_hash(hash);
    map->oom = false;
    size_t i = hash & map->mask;
    while (1) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (!bucket->dib) {
            return false;
        }
        void *b_key = duo_hashmap_bucket_key(map, bucket);
        if (bucket->hash == hash && duo_hashmap_compare_keys(map, key, b_key) == 0) {
            // Destroy key and value internals (Issue #38)
            if (map->key_free) {
                map->key_free(b_key);
            }
            if (map->val_free && map->val_size > 0) {
                map->val_free(duo_hashmap_bucket_val(map, bucket));
            }
            bucket->dib = 0;
            while (1) {
                struct duo_hash_bucket *prev = bucket;
                i = (i + 1) & map->mask;
                bucket = duo_hashmap_bucket_at(map, i);
                if (bucket->dib <= 1) {
                    prev->dib = 0;
                    break;
                }
                DUO_MEMMOVE(prev, bucket, map->bucketsz);
                prev->dib--;
            }
            map->count--;
            if (map->nbuckets > map->cap && map->count <= map->shrinkat) {
                duo_hashmap_resize(map, map->nbuckets / 2);
            }
            return true;
        }
        i = (i + 1) & map->mask;
    }
}

/**
 * @brief Removes an entry matching key and invokes key_free and val_free.
 *
 * @param map Pointer to the hash map.
 * @param key Pointer to key to delete.
 * @return true if found and deleted, false otherwise.
 */
static inline bool duo_hashmap_delete(duo_hashmap_t *map, const void *key) {
    return duo_hashmap_delete_with_hash(map, key, duo_hashmap_calc_hash(map, key));
}

/**
 * @brief Extracts an entry without invoking key_free or val_free, copying bytes to caller buffers.
 *
 * @param map     Pointer to the hash map.
 * @param key     Pointer to key to extract.
 * @param hash    Precomputed 64-bit hash.
 * @param out_key Destination buffer for key bytes (or NULL).
 * @param out_val Destination buffer for value bytes (or NULL).
 * @return true if found and extracted, false otherwise.
 */
static inline bool duo_hashmap_extract_with_hash(duo_hashmap_t *map, const void *key, uint64_t hash, void *out_key, void *out_val) {
    hash = duo_hashmap_clip_hash(hash);
    size_t i = hash & map->mask;
    while (1) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (!bucket->dib) return false;
        void *b_key = duo_hashmap_bucket_key(map, bucket);
        if (bucket->hash == hash && duo_hashmap_compare_keys(map, key, b_key) == 0) {
            if (out_key) DUO_MEMCPY(out_key, b_key, map->key_size);
            if (out_val && map->val_size > 0) DUO_MEMCPY(out_val, duo_hashmap_bucket_val(map, bucket), map->val_size);
            bucket->dib = 0;
            while (1) {
                struct duo_hash_bucket *prev = bucket;
                i = (i + 1) & map->mask;
                bucket = duo_hashmap_bucket_at(map, i);
                if (bucket->dib <= 1) {
                    prev->dib = 0;
                    break;
                }
                DUO_MEMMOVE(prev, bucket, map->bucketsz);
                prev->dib--;
            }
            map->count--;
            if (map->nbuckets > map->cap && map->count <= map->shrinkat) {
                duo_hashmap_resize(map, map->nbuckets / 2);
            }
            return true;
        }
        i = (i + 1) & map->mask;
    }
}

/**
 * @brief Extracts an entry without invoking destructors, copying key and value bytes out.
 *
 * @param map     Pointer to the hash map.
 * @param key     Pointer to key to extract.
 * @param out_key Destination buffer for key bytes (or NULL).
 * @param out_val Destination buffer for value bytes (or NULL).
 * @return true if found and extracted, false otherwise.
 */
static inline bool duo_hashmap_extract(duo_hashmap_t *map, const void *key, void *out_key, void *out_val) {
    return duo_hashmap_extract_with_hash(map, key, duo_hashmap_calc_hash(map, key), out_key, out_val);
}

/**
 * @brief Returns the number of active Key-Value pairs in the hash map.
 *
 * @param map Pointer to the hash map.
 * @return Active element count.
 */
static inline size_t duo_hashmap_count(const duo_hashmap_t *map) {
    return map->count;
}

/**
 * @brief Frees all key and value resources (via key_free and val_free) and deallocates the map.
 *
 * @param map Pointer to the hash map (safe to pass NULL).
 */
static inline void duo_hashmap_free(duo_hashmap_t *map) {
    if (!map) return;
    duo_hashmap_free_elements(map);
    DUO_FREE(map->buckets);
    DUO_FREE(map);
}

/**
 * @brief Checks if the most recent allocation or table resize attempt failed due to OOM.
 *
 * @param map Pointer to the hash map.
 * @return true if last operation encountered out-of-memory, false otherwise.
 */
static inline bool duo_hashmap_oom(const duo_hashmap_t *map) {
    return map->oom;
}

/**
 * @brief Iterates over all active Key-Value pairs using an iteration callback.
 *
 * @param map   Pointer to the hash map.
 * @param iter  Callback function: `bool iter(const void *key, void *val, void *udata)`.
 *              Returning false aborts iteration early.
 * @param udata User data forwarded to iter callback.
 * @return true if all items were scanned, false if aborted early.
 */
static inline bool duo_hashmap_scan(duo_hashmap_t *map, 
    bool (*iter)(const void *key, void *val, void *udata), void *udata)
{
    for (size_t i = 0; i < map->nbuckets; i++) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (bucket->dib) {
            void *k = duo_hashmap_bucket_key(map, bucket);
            void *v = map->val_size > 0 ? duo_hashmap_bucket_val(map, bucket) : NULL;
            if (!iter(k, v, udata)) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Iterates through the map one element at a time using an integer cursor.
 *
 * The caller initializes `size_t i = 0` before the first call.
 *
 * @param map     Pointer to the hash map.
 * @param i       Pointer to the cursor index (incremented across calls).
 * @param out_key Destination pointer to receive address of current key.
 * @param out_val Destination pointer to receive address of current value.
 * @return true if an element was yielded, false if iteration is complete.
 */
static inline bool duo_hashmap_iter(duo_hashmap_t *map, size_t *i, void **out_key, void **out_val) {
    struct duo_hash_bucket *bucket;
    do {
        if (*i >= map->nbuckets) return false;
        bucket = duo_hashmap_bucket_at(map, *i);
        (*i)++;
    } while (!bucket->dib);

    if (out_key) *out_key = duo_hashmap_bucket_key(map, bucket);
    if (out_val) *out_val = map->val_size > 0 ? duo_hashmap_bucket_val(map, bucket) : NULL;
    return true;
}

//-----------------------------------------------------------------------------
// Issue #41: Predicate-Based Filtering
// In tidwall/hashmap.c Issue #41 ("How to implement hashmap_filter"), deleting
// items during manual iteration (hashmap_iter) causes backward-shift skipping
// and table-shrink invalidations. duo_hashmap_filter provides a safe, atomic, O(N)
// predicate filter that deletes matching items (invoking key_free and val_free)
// and re-packs surviving items in a single pass without any iterator hazards.
//-----------------------------------------------------------------------------

/**
 * @brief Atomically filters the map in-place using a predicate callback (Issue #41).
 *
 * Evaluates remove_pred on every active Key-Value pair.
 * For each pair where remove_pred returns true:
 * - key_free is invoked on the key.
 * - val_free is invoked on the value.
 * - The pair is removed.
 *
 * Surviving elements are compacted and re-packed via duo_hashmap_resize0.
 * If element count falls below shrink threshold (DUO_HASHMAP_SHRINK_AT = 10%),
 * the table automatically shrinks to reclaim memory.
 *
 * @param map         Pointer to the hash map.
 * @param remove_pred Callback returning true to delete an item, false to keep it.
 * @param udata       User data passed to remove_pred.
 * @return Total number of items removed.
 */
static inline size_t duo_hashmap_filter(duo_hashmap_t *map,
    bool (*remove_pred)(const void *key, void *val, void *udata), void *udata)
{
    if (!map || map->count == 0 || !remove_pred) {
        return 0;
    }

    size_t removed = 0;
    for (size_t i = 0; i < map->nbuckets; i++) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(map, i);
        if (bucket->dib) {
            void *k = duo_hashmap_bucket_key(map, bucket);
            void *v = map->val_size > 0 ? duo_hashmap_bucket_val(map, bucket) : NULL;
            if (remove_pred(k, v, udata)) {
                if (map->key_free) {
                    map->key_free(k);
                }
                if (map->val_free && map->val_size > 0) {
                    map->val_free(v);
                }
                bucket->dib = 0;
                removed++;
            }
        }
    }

    if (removed == 0) {
        return 0;
    }

    map->count -= removed;

    if (map->count == 0) {
        duo_hashmap_clear(map, false);
        return removed;
    }

    // Determine target capacity (shrink if count <= shrink threshold)
    size_t target_cap = map->nbuckets;
    while (target_cap > map->cap && map->count <= (size_t)(target_cap * DUO_HASHMAP_SHRINK_AT)) {
        target_cap /= 2;
    }
    if (target_cap < map->cap) {
        target_cap = map->cap;
    }

    // Rebuild surviving elements into a clean, hole-free bucket table
    duo_hashmap_resize0(map, target_cap);
    return removed;
}

/**
 * @brief Returns the total allocated bucket capacity of the table.
 *
 * @param map Pointer to the hash map.
 * @return Total bucket slot count.
 */
static inline size_t duo_hashmap_nbuckets(const duo_hashmap_t *map) {
    return map->nbuckets;
}

/**
 * @brief Returns the total allocated bucket capacity (semantic alias for duo_hashmap_nbuckets).
 *
 * @param map Pointer to the hash map.
 * @return Total bucket slot count.
 */
static inline size_t duo_hashmap_capacity(const duo_hashmap_t *map) {
    return map->nbuckets;
}

/* ============================================================================
 * PURE C ASSOCIATIVE SET LAYER: duo_hashset_t
 *
 * High-performance, zero-overhead Robin Hood hash set mapping cleanly over
 * the duo_hashmap engine with val_size = 0.
 * ============================================================================ */

/**
 * @brief Allocates and initializes a new hash set using DuoSTL allocators (DUO_MALLOC / DUO_FREE).
 *
 * @param item_size Size of each set element in bytes (must be > 0).
 * @param cap       Minimum initial bucket capacity (rounds up to power of 2, min 16).
 * @param seed0     First 64-bit seed.
 * @param seed1     Second 64-bit seed.
 * @param hash      Custom hash function (or NULL for default xxHash3).
 * @param compare   Custom element compare function (or NULL for memcmp).
 * @param item_free Callback to free dynamic element resources upon removal/destruction.
 * @param udata     User data passed to compare callback.
 * @return Pointer to newly allocated duo_hashset_t, or NULL on OOM.
 */
static inline duo_hashset_t *duo_hashset_new(
    size_t item_size, 
    size_t cap, 
    uint64_t seed0, 
    uint64_t seed1,
    uint64_t (*hash)(const void *item, uint64_t seed0, uint64_t seed1),
    int (*compare)(const void *a, const void *b, void *udata),
    void (*item_free)(void *item),
    void *udata)
{
    return (duo_hashset_t*)duo_hashmap_new(item_size, 0, cap,
        seed0, seed1, hash, compare, item_free, NULL, udata);
}


/**
 * @brief Frees all element resources (via item_free) and deallocates the set.
 *
 * @param set Pointer to the hash set (safe to pass NULL).
 */
static inline void duo_hashset_free(duo_hashset_t *set) {
    duo_hashmap_free((duo_hashmap_t*)set);
}

/**
 * @brief Clears all elements from the set, invoking item_free on every element.
 *
 * @param set        Pointer to the hash set.
 * @param update_cap If true, retains current bucket capacity; if false, resets to initial capacity.
 */
static inline void duo_hashset_clear(duo_hashset_t *set, bool update_cap) {
    duo_hashmap_clear((duo_hashmap_t*)set, update_cap);
}

/**
 * @brief Returns the number of active elements currently in the hash set.
 *
 * @param set Pointer to the hash set.
 * @return Number of stored elements.
 */
static inline size_t duo_hashset_count(const duo_hashset_t *set) {
    return duo_hashmap_count((const duo_hashmap_t*)set);
}

/**
 * @brief Returns the total allocated bucket capacity of the set.
 *
 * @param set Pointer to the hash set.
 * @return Total bucket slot count.
 */
static inline size_t duo_hashset_capacity(const duo_hashset_t *set) {
    return duo_hashmap_capacity((const duo_hashmap_t*)set);
}

/**
 * @brief Checks if the most recent allocation or table resize attempt failed due to OOM.
 *
 * @param set Pointer to the hash set.
 * @return true if last operation encountered out-of-memory, false otherwise.
 */
static inline bool duo_hashset_oom(const duo_hashset_t *set) {
    return duo_hashmap_oom((const duo_hashmap_t*)set);
}

/**
 * @brief Sets the geometric power-of-two growth exponent for capacity resizing.
 *
 * @param set   Pointer to the hash set.
 * @param power Power-of-two multiplier exponent (1 <= power <= 16; default 1 = double).
 */
static inline void duo_hashset_set_grow_by_power(duo_hashset_t *set, size_t power) {
    duo_hashmap_set_grow_by_power((duo_hashmap_t*)set, power);
}

/**
 * @brief Configures the target load factor triggering capacity growth.
 *
 * @param set    Pointer to the hash set.
 * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
 */
static inline void duo_hashset_set_load_factor(duo_hashset_t *set, double factor) {
    duo_hashmap_set_load_factor((duo_hashmap_t*)set, factor);
}

/**
 * @brief Inserts an element into the set using a precomputed 64-bit hash.
 *
 * Employs Robin Hood hashing with DIB displacement.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element bytes.
 * @param hash Precomputed 64-bit hash of item.
 * @return true on success, false on OOM.
 */
static inline bool duo_hashset_insert_with_hash(duo_hashset_t *set, const void *item, uint64_t hash) {
    return duo_hashmap_set_with_hash((duo_hashmap_t*)set, item, NULL, hash);
}

/**
 * @brief Inserts an element into the set.
 *
 * Automatically hashes item using the configured hash callback (or xxHash3).
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element bytes.
 * @return true on success, false on OOM.
 */
static inline bool duo_hashset_insert(duo_hashset_t *set, const void *item) {
    return duo_hashmap_set((duo_hashmap_t*)set, item, NULL);
}

/**
 * @brief Tests whether an element exists in the set using a precomputed 64-bit hash.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to search for.
 * @param hash Precomputed 64-bit hash.
 * @return true if found, false otherwise.
 */
static inline bool duo_hashset_contains_with_hash(const duo_hashset_t *set, const void *item, uint64_t hash) {
    return duo_hashmap_get_with_hash((const duo_hashmap_t*)set, item, hash) != NULL;
}

/**
 * @brief Tests whether an element exists in the set.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to search for.
 * @return true if found, false otherwise.
 */
static inline bool duo_hashset_contains(const duo_hashset_t *set, const void *item) {
    return duo_hashmap_contains((const duo_hashmap_t*)set, item);
}

/**
 * @brief Retrieves an immutable read-only pointer to the stored element matching item using a precomputed hash.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to search for.
 * @param hash Precomputed 64-bit hash.
 * @return Const pointer to the stored element in the bucket, or NULL if not found.
 */
static inline const void *duo_hashset_get_with_hash(const duo_hashset_t *set, const void *item, uint64_t hash) {
    return (const void*)duo_hashmap_get_with_hash((const duo_hashmap_t*)set, item, hash);
}

/**
 * @brief Retrieves an immutable read-only pointer to the stored element matching item.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to search for.
 * @return Const pointer to the stored element in the bucket, or NULL if not found.
 */
static inline const void *duo_hashset_get(const duo_hashset_t *set, const void *item) {
    return (const void*)duo_hashmap_get((const duo_hashmap_t*)set, item);
}

/**
 * @brief Returns an immutable read-only pointer to the element at bucket index modulo capacity.
 *
 * @param set   Pointer to the hash set.
 * @param index Bucket index (modulo table capacity).
 * @return Const pointer to element at slot, or NULL if bucket is empty.
 */
static inline const void *duo_hashset_probe_const(const duo_hashset_t *set, size_t index) {
    return (const void*)duo_hashmap_probe((const duo_hashmap_t*)set, index);
}

/**
 * @brief Returns a mutable pointer to the element at bucket index modulo capacity.
 *
 * @param set   Pointer to the hash set.
 * @param index Bucket index (modulo table capacity).
 * @return Pointer to element at slot, or NULL if bucket is empty.
 */
static inline void *duo_hashset_probe(const duo_hashset_t *set, size_t index) {
    return duo_hashmap_probe((const duo_hashmap_t*)set, index);
}

/**
 * @brief Removes an element matching item using a precomputed hash and backward-shift deletion.
 *
 * Invokes item_free on the element if configured.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to remove.
 * @param hash Precomputed 64-bit hash.
 * @return true if element was found and removed, false if not found.
 */
static inline bool duo_hashset_remove_with_hash(duo_hashset_t *set, const void *item, uint64_t hash) {
    return duo_hashmap_delete_with_hash((duo_hashmap_t*)set, item, hash);
}

/**
 * @brief Removes an element matching item from the set.
 *
 * Invokes item_free on the element if configured.
 *
 * @param set  Pointer to the hash set.
 * @param item Pointer to element to remove.
 * @return true if element was found and removed, false if not found.
 */
static inline bool duo_hashset_remove(duo_hashset_t *set, const void *item) {
    return duo_hashmap_delete((duo_hashmap_t*)set, item);
}

/**
 * @brief Extracts an element without invoking item_free, copying element bytes to out_item buffer.
 *
 * @param set      Pointer to the hash set.
 * @param item     Pointer to element to search for.
 * @param hash     Precomputed 64-bit hash.
 * @param out_item Destination buffer for element bytes (or NULL).
 * @return true if found and extracted, false otherwise.
 */
static inline bool duo_hashset_extract_with_hash(duo_hashset_t *set, const void *item, uint64_t hash, void *out_item) {
    return duo_hashmap_extract_with_hash((duo_hashmap_t*)set, item, hash, out_item, NULL);
}

/**
 * @brief Extracts an element without invoking item_free, copying element bytes to out_item buffer.
 *
 * @param set      Pointer to the hash set.
 * @param item     Pointer to element to search for.
 * @param out_item Destination buffer for element bytes (or NULL).
 * @return true if found and extracted, false otherwise.
 */
static inline bool duo_hashset_extract(duo_hashset_t *set, const void *item, void *out_item) {
    return duo_hashmap_extract((duo_hashmap_t*)set, item, out_item, NULL);
}

/**
 * @brief Iterates over all active elements in the hash set using an iteration callback.
 *
 * @param set   Pointer to the hash set.
 * @param iter  Callback: bool (*iter)(const void *item, void *udata). Returning false aborts early.
 * @param udata User data forwarded to iter callback.
 * @return true if all items were scanned, false if aborted early.
 */
static inline bool duo_hashset_scan(duo_hashset_t *set, 
    bool (*iter)(const void *item, void *udata), void *udata)
{
    for (size_t i = 0; i < set->nbuckets; i++) {
        struct duo_hash_bucket *bucket = duo_hashmap_bucket_at(set, i);
        if (bucket->dib) {
            void *item = duo_hashmap_bucket_key(set, bucket);
            if (!iter(item, udata)) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Iterates through the hash set one element at a time using an integer cursor.
 *
 * The caller initializes `size_t i = 0` before the first call.
 *
 * @param set      Pointer to the hash set.
 * @param i        Pointer to cursor index (incremented across calls).
 * @param out_item Destination pointer to receive address of current element.
 * @return true if an element was yielded, false if iteration is complete.
 */
static inline bool duo_hashset_iter(duo_hashset_t *set, size_t *i, void **out_item) {
    return duo_hashmap_iter((duo_hashmap_t*)set, i, out_item, NULL);
}

/**
 * @brief Helper context for duo_hashset_filter bridging to duo_hashmap_filter.
 */
struct duo_hashset_filter_ctx {
    bool (*remove_pred)(const void *item, void *udata);
    void *udata;
};

static inline bool duo_hashset_filter_trampoline(const void *key, void *val, void *udata) {
    (void)val;
    struct duo_hashset_filter_ctx *ctx = (struct duo_hashset_filter_ctx*)udata;
    return ctx->remove_pred(key, ctx->udata);
}

/**
 * @brief Atomically filters the set in-place using a predicate callback (Issue #41).
 *
 * Evaluates remove_pred on every active element. If true, invokes item_free and removes the item.
 * Compresses and re-packs surviving elements into a clean table in a single pass.
 *
 * @param set         Pointer to the hash set.
 * @param remove_pred Callback returning true to delete an item, false to keep it.
 * @param udata       User data passed to remove_pred.
 * @return Total number of items removed.
 */
static inline size_t duo_hashset_filter(duo_hashset_t *set,
    bool (*remove_pred)(const void *item, void *udata), void *udata)
{
    struct duo_hashset_filter_ctx ctx = { remove_pred, udata };
    return duo_hashmap_filter((duo_hashmap_t*)set, duo_hashset_filter_trampoline, &ctx);
}

//-----------------------------------------------------------------------------
// SipHash-2-4 Implementation
//-----------------------------------------------------------------------------
static inline uint64_t SIP64(const uint8_t *in, const size_t inlen, uint64_t seed0, uint64_t seed1) {
#define DUO_U8TO64_LE(p) \
    (  (((uint64_t)((p)[0]))      ) | (((uint64_t)((p)[1])) <<  8) | \
       (((uint64_t)((p)[2])) << 16) | (((uint64_t)((p)[3])) << 24) | \
       (((uint64_t)((p)[4])) << 32) | (((uint64_t)((p)[5])) << 40) | \
       (((uint64_t)((p)[6])) << 48) | (((uint64_t)((p)[7])) << 56) )

#define DUO_U32TO8_LE(p, v) \
    do { (p)[0] = (uint8_t)((v)); \
         (p)[1] = (uint8_t)((v) >> 8); \
         (p)[2] = (uint8_t)((v) >> 16); \
         (p)[3] = (uint8_t)((v) >> 24); } while(0)

#define DUO_U64TO8_LE(p, v) \
    do { DUO_U32TO8_LE((p), (uint32_t)((v))); \
         DUO_U32TO8_LE((p) + 4, (uint32_t)((v) >> 32)); } while(0)

#define DUO_ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define DUO_SIPROUND \
    do { v0 += v1; v1 = DUO_ROTL(v1, 13); \
         v1 ^= v0; v0 = DUO_ROTL(v0, 32); \
         v2 += v3; v3 = DUO_ROTL(v3, 16); \
         v3 ^= v2; \
         v0 += v3; v3 = DUO_ROTL(v3, 21); \
         v3 ^= v0; \
         v2 += v1; v1 = DUO_ROTL(v1, 17); \
         v1 ^= v2; v2 = DUO_ROTL(v2, 32); } while(0)

    uint64_t k0 = seed0;
    uint64_t k1 = seed1;
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;
    uint64_t b = ((uint64_t)inlen) << 56;
    const uint8_t *end = in + inlen - (inlen % sizeof(uint64_t));

    while (in < end) {
        uint64_t m = DUO_U8TO64_LE(in);
        v3 ^= m;
        DUO_SIPROUND; DUO_SIPROUND;
        v0 ^= m;
        in += sizeof(uint64_t);
    }

    const int left = inlen & 7;
    switch (left) {
    case 7: b |= ((uint64_t)in[6]) << 48; /* fall through */
    case 6: b |= ((uint64_t)in[5]) << 40; /* fall through */
    case 5: b |= ((uint64_t)in[4]) << 32; /* fall through */
    case 4: b |= ((uint64_t)in[3]) << 24; /* fall through */
    case 3: b |= ((uint64_t)in[2]) << 16; /* fall through */
    case 2: b |= ((uint64_t)in[1]) <<  8; /* fall through */
    case 1: b |= ((uint64_t)in[0]); break;
    case 0: break;
    }

    v3 ^= b;
    DUO_SIPROUND; DUO_SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    DUO_SIPROUND; DUO_SIPROUND; DUO_SIPROUND; DUO_SIPROUND;
    b = v0 ^ v1 ^ v2 ^ v3;
    uint64_t out = 0;
    DUO_U64TO8_LE((uint8_t*)&out, b);

#undef DUO_U8TO64_LE
#undef DUO_U32TO8_LE
#undef DUO_U64TO8_LE
#undef DUO_ROTL
#undef DUO_SIPROUND

    return out;
}

//-----------------------------------------------------------------------------
// MurmurHash3_86_128 Implementation (Hardened against Issue #49)
// In tidwall/hashmap.c Issue #49 ("hashmap_murmur: unaligned uint32_t loads
// and signed uint8_t << 24 in MM86128"):
// 1. Unaligned blocks: tidwall type-puns 'const uint32_t *blocks', which is UB
//    and causes SIGBUS on strict alignment targets (e.g. ARM) or UBSan errors.
//    DuoSTL uses memcpy to safely load 32-bit words without alignment constraints.
// 2. Signed shift overflow: 'tail[i] << 24' promotes unsigned char to signed int.
//    If byte >= 128 (e.g. 0x80), 128 << 24 is undefined behavior in signed 32-bit int.
//    DuoSTL explicitly casts each byte to unsigned '(uint32_t)tail[i] << shift'.
//-----------------------------------------------------------------------------
static inline uint64_t MM86128(const void *key, const int len, uint32_t seed) {
#define DUO_ROTL32(x, r) ((uint32_t)(((x) << (r)) | ((x) >> (32 - (r)))))
#define DUO_FMIX32(h) do { (h) ^= (h) >> 16; (h) *= 0x85ebca6b; (h) ^= (h) >> 13; (h) *= 0xc2b2ae35; (h) ^= (h) >> 16; } while(0)

    const uint8_t * data = (const uint8_t*)key;
    const int nblocks = len / 16;
    uint32_t h1 = seed;
    uint32_t h2 = seed;
    uint32_t h3 = seed;
    uint32_t h4 = seed;
    uint32_t c1 = 0x239b961b; 
    uint32_t c2 = 0xab0e9789;
    uint32_t c3 = 0x38b34ae5; 
    uint32_t c4 = 0xa1e38b93;

    for (int i = 0; i < nblocks; i++) {
        uint32_t k1, k2, k3, k4;
        DUO_MEMCPY(&k1, data + i*16 + 0, sizeof(uint32_t));
        DUO_MEMCPY(&k2, data + i*16 + 4, sizeof(uint32_t));
        DUO_MEMCPY(&k3, data + i*16 + 8, sizeof(uint32_t));
        DUO_MEMCPY(&k4, data + i*16 + 12, sizeof(uint32_t));

        k1 *= c1; k1 = DUO_ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
        h1 = DUO_ROTL32(h1, 19); h1 += h2; h1 = h1*5 + 0x561ccd1b;

        k2 *= c2; k2 = DUO_ROTL32(k2, 16); k2 *= c3; h2 ^= k2;
        h2 = DUO_ROTL32(h2, 17); h2 += h3; h2 = h2*5 + 0x0bcaa747;

        k3 *= c3; k3 = DUO_ROTL32(k3, 17); k3 *= c4; h3 ^= k3;
        h3 = DUO_ROTL32(h3, 15); h3 += h4; h3 = h3*5 + 0x96cd1c35;

        k4 *= c4; k4 = DUO_ROTL32(k4, 18); k4 *= c1; h4 ^= k4;
        h4 = DUO_ROTL32(h4, 13); h4 += h1; h4 = h4*5 + 0x32ac3b17;
    }

    const uint8_t * tail = data + nblocks*16;
    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;
    switch(len & 15) {
    case 15: k4 ^= (uint32_t)tail[14] << 16; /* fall through */
    case 14: k4 ^= (uint32_t)tail[13] << 8;  /* fall through */
    case 13: k4 ^= (uint32_t)tail[12] << 0;
             k4 *= c4; k4 = DUO_ROTL32(k4, 18); k4 *= c1; h4 ^= k4;
             /* fall through */
    case 12: k3 ^= (uint32_t)tail[11] << 24; /* fall through */
    case 11: k3 ^= (uint32_t)tail[10] << 16; /* fall through */
    case 10: k3 ^= (uint32_t)tail[ 9] << 8;  /* fall through */
    case  9: k3 ^= (uint32_t)tail[ 8] << 0;
             k3 *= c3; k3 = DUO_ROTL32(k3, 17); k3 *= c4; h3 ^= k3;
             /* fall through */
    case  8: k2 ^= (uint32_t)tail[ 7] << 24; /* fall through */
    case  7: k2 ^= (uint32_t)tail[ 6] << 16; /* fall through */
    case  6: k2 ^= (uint32_t)tail[ 5] << 8;  /* fall through */
    case  5: k2 ^= (uint32_t)tail[ 4] << 0;
             k2 *= c2; k2 = DUO_ROTL32(k2, 16); k2 *= c3; h2 ^= k2;
             /* fall through */
    case  4: k1 ^= (uint32_t)tail[ 3] << 24; /* fall through */
    case  3: k1 ^= (uint32_t)tail[ 2] << 16; /* fall through */
    case  2: k1 ^= (uint32_t)tail[ 1] << 8;  /* fall through */
    case  1: k1 ^= (uint32_t)tail[ 0] << 0;
             k1 *= c1; k1 = DUO_ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
             /* fall through */
    };
    h1 ^= (uint32_t)len; h2 ^= (uint32_t)len; h3 ^= (uint32_t)len; h4 ^= (uint32_t)len;
    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;
    DUO_FMIX32(h1); DUO_FMIX32(h2); DUO_FMIX32(h3); DUO_FMIX32(h4);
    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

#undef DUO_ROTL32
#undef DUO_FMIX32

    return (((uint64_t)h2)<<32)|h1;
}

//-----------------------------------------------------------------------------
// xxHash3 Implementation (delegates to centralized duo_alloc.h engine)
//-----------------------------------------------------------------------------
static inline uint64_t xxh3(const void* data, size_t len, uint64_t seed) {
    return duo_xxh3_impl(data, len, seed);
}

/**
 * @brief Computes a 64-bit SipHash-2-4 digest.
 *
 * @param data  Pointer to the input buffer.
 * @param len   Length of the input buffer in bytes.
 * @param seed0 First 64-bit seed.
 * @param seed1 Second 64-bit seed.
 * @return 64-bit hash value.
 */
static inline uint64_t duo_hash_sip(const void *data, size_t len, uint64_t seed0, uint64_t seed1) {
    return SIP64((const uint8_t*)data, len, seed0, seed1);
}

/**
 * @brief Computes a 64-bit MurmurHash3_86_128 digest (hardened against Issue #49).
 *
 * Safe against unaligned memory accesses and integer promotion signed shift overflow.
 *
 * @param data  Pointer to the input buffer.
 * @param len   Length of the input buffer in bytes.
 * @param seed0 32-bit seed (truncated from uint64_t).
 * @param seed1 Unused (present for hash signature uniformity).
 * @return 64-bit hash value.
 */
static inline uint64_t duo_hash_murmur(const void *data, size_t len, uint64_t seed0, uint64_t seed1) {
    (void)seed1;
    return MM86128(data, (int)len, (uint32_t)seed0);
}


#if defined(__cplusplus)
}
#endif

#endif /* DUO_HASH_H */
