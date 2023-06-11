// Copyright (c) 2023 Steffen Illhardt
// Licensed under the MIT license ( https://opensource.org/license/mit/ ).

#include <stdlib.h>
#include <string.h>
#include "hm.h"

#if !defined(HASHMAP_12AA98F5_9135_48EA_9AD3_8619146FAEAE)
#  error "hash map version mismatch source <=> header"
#elif defined(__GNUC__) || defined(__clang__)
#  define HM_PRIVATE static inline __attribute__((always_inline))
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-align" // some casts intentionally increase the alignment
#  pragma GCC diagnostic ignored "-Wdeclaration-after-statement" // C99 is required anyway, no issue here
#  pragma GCC diagnostic ignored "-Wpadded"
#  if defined(__clang_major__) && (__clang_major__ >= 16)
#    pragma clang diagnostic ignored "-Wunsafe-buffer-usage" // tons of pointer stuff in this code because it's C, not C++; so ... shh clang!
#  endif
#elif defined(_MSC_VER)
#  define HM_PRIVATE static __forceinline
#  pragma warning(push)
#  pragma warning(disable : 4706) // assignment within conditional expression
#  pragma warning(disable : 4711) // function selected for inline expansion
#  pragma warning(disable : 4820) // padding added
#  pragma warning(disable : 5045) // spectre mitigation possibly inserted
#  pragma warning(disable : 6001) // using uninitialized memory (deallocations in `destroy_values_()`)
#else
#  define HM_PRIVATE static inline
#endif

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~ private interface ~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// The hash map is implemented with conflicting values resolved by chaining in singly linked stacks. Links are indices (offsets) in an array rather than pointers.
// Values of node indices saved in the structure members `pBuckets`, `recyclingBucket`, `lastUsed`, and `nextIdx` are 1-based.
// That means, each value is the actual (0-based) index + 1. This is because zero-values have a special meaning:
// - In `node_t::nextIdx` 0 indicates the ground of the stack of nodes that are linked together.
// - In `hm_t::pBuckets` 0 indicates that no node is linked yet.
// - In `hm_t::recyclingBucket` 0 indicates that no nodes are in the stack of previously removed nodes.
// - In `hm_t::lastUsed` 0 indicates that no nodes are used yet.
// Since we are working with stacks, the term "node" is used instead of "slot"

// clang-format off

// Structure type which contains the value, the hash, and the link to the next node.
typedef  struct hm_node
{
    struct hm_item_spec  dat;           // key and value along with their sizes, we treat hs_item_spec as a subset of hm_item_spec (first 2 members) for the hash set interface, a NULL pointer for the key member separates removed from still used nodes
    uint64_t             hash;          // hash value of the key
    uint32_t             alignedValCap; // 4-byte aligned capacity of `dat.val`, floored
    uint32_t             nextIdx;       // 1-based index linking the next node in the stack of nodes, 0 indicates the ground of the stack
}  node_t;

// Structure type which contains the internal buffers and values necessary to specify the hash map (and the wrapped hash set).
struct hm_spec
{
    uint64_t      hashSeed;        // seed used in the hashing function
    hash_func_t   hashFunc;        // pointer to the hashing function used to calculate the hash values of the keys
    equ_comp_t    compFunc;
    node_t       *pNodes;          // all nodes in a contiguous memory object (array), they are later chained into stacks of different order
    uint32_t     *pBuckets;        // array of 1-based indices linking the top nodes of stacked nodes, 0 indicates that no node is linked yet
    uint32_t      nodesCap;        // maximum number of nodes the hash map can contain without resizing
    uint32_t      bucketsMaxIdx;   // maximum index in pBuckets, always (2^n - 1) because it's used to mask the hash to get the index in pBuckets
    uint32_t      recyclingBucket; // 1-based top index of the stack of nodes that have been removed and can be reused, 0 indicates an empty stack
    uint32_t      nodesCnt;        // current number of used nodes
    uint32_t      lastUsed;        // 1-based index of the last node ever used, 0 indicates that no node is used yet, it's also the real (0-based) index of a new uninitialized node
};

#define MIN_NODES_CAP    UINT32_C(192) // initial number of nodes (slots, elements, items), 3/4 of MIN_BUCKETS_CAP to keep stack sizes low
#define MIN_BUCKETS_CAP  UINT32_C(256) // initial number of buckets (links to the top node of a stack each), must be a power of 2

// clang-format on

// Calculate a 64-bit hash value from the specified byte sequence.
// The FNV-1a algorithm is chosen for the sake of ease. It's the fallback if no custom hashing function has been defined.
// However, if long keys are expected you may want to use a vectorized hashing algorithm (like XXH3). Furthermore, FNV-1a is not randomized.
// FNV-1a was originated by an idea of Glenn Fowler and Phong Vo, improved by Landon Curt Noll, and has been released into the public domain (CC0).
HM_PRIVATE uint64_t get_hash_(const void *const key, const size_t keyLen, const uint64_t hashSeed)
{
  (void)hashSeed;
  uint64_t hash = UINT64_C(0xCBF29CE484222325);
  for (const uint8_t *byteIt = (const uint8_t *)key, *const end = byteIt + keyLen; byteIt < end; ++byteIt)
    hash = (hash ^ *byteIt) * UINT64_C(0x00000100000001B3);

  return hash;
}

// Determine the equality of two key values.
// The `memcmp()` function is the fallback if no custom comparison function has been defined.
// However, if keys contain paddings with undefined content (e.g. members in structs might be padded) you should use a suitable algorithm for the comparison.
HM_PRIVATE bool keys_equal_(const void *const key1, const void *const key2, const size_t keyLen)
{
  return memcmp(key1, key2, keyLen) == 0;
}

// Allocate memory, copy the specified byte sequences, and append terminating null characters suitable for any string type.
HM_PRIVATE void *pair_dup_(const void *const key, const uint32_t keyLen, const void *const val, const uint32_t valLen, uint32_t *const pVal4ByteAligned)
{
  const size_t key4ByteAligned = keyLen & ~UINT32_C(3); // 4-byte aligned length, floored
  if (val == NULL) // we only need memory for the key
  {
    uint8_t *const newKey = malloc(key4ByteAligned + 4); // UTF-32 (worst case) is 4-byte aligned, adding 4 bytes for the terminating null is sufficient, for any other encoding we allocate at most 3 bytes too many
    if (newKey == NULL)
      return NULL;

    *(uint32_t *)(newKey + key4ByteAligned) = UINT32_C(0); // set the additional 4 bytes to 0 at once; before memcpy is called as it may overwrite up to 3 of these bytes
    // NOLINTNEXTLINE
    return memcpy(newKey, key, keyLen); // clang-tidy prefers memcpy_s, however there is no doubt that buffer bounds are respected here
  }

  // we allocate memory for both key and value at once, 4-byte aligned each
  *pVal4ByteAligned = valLen & ~UINT32_C(3);
  uint8_t *const newPair = malloc(key4ByteAligned + *pVal4ByteAligned + 8);
  if (newPair == NULL)
    return NULL;

  uint8_t *const keyPtr = newPair + *pVal4ByteAligned + 4;
  *(uint32_t *)(keyPtr + key4ByteAligned) = UINT32_C(0);
  memcpy(keyPtr, key, keyLen); // NOLINT
  *(uint32_t *)(newPair + *pVal4ByteAligned) = UINT32_C(0);
  return memcpy(newPair, val, valLen); // NOLINT
}

// Deallocate memory of a single item.
HM_PRIVATE void pair_free_(const node_t *const pNode)
{
  free(pNode->dat.val != NULL ? pNode->dat.val : (void *)(intptr_t)(pNode->dat.key));
}

// Deallocate any remaining keys and values. Detached values are of course unaffected.
HM_PRIVATE void destroy_values_(const hmc_t hm)
{
  for (const node_t *nodeIt = hm->pNodes, *const end = nodeIt + hm->lastUsed; nodeIt < end; ++nodeIt)
    if (nodeIt->dat.key != NULL)
      pair_free_(nodeIt);
}

// Update the value associated with an existing key.
HM_PRIVATE bool assign_dat_(node_t *const pNode, const void *const val, const uint32_t valLen)
{
  if (val == NULL && pNode->dat.val == NULL)
    return true;

  if (val != NULL && pNode->dat.val != NULL) // check if `pNode->dat.val` can be reused
  {
    const uint32_t val4ByteAligned = valLen & ~UINT32_C(3); // 4-byte aligned length, floored
    if (val4ByteAligned <= pNode->alignedValCap) // the allocated memory of `pNode->dat.val` can be reused (see `pair_dup_()` which allocated 4-byte aligned memory)
    {
      *(uint32_t *)(((uint8_t *)(pNode->dat.val)) + val4ByteAligned) = UINT32_C(0); // set the last 4 bytes to 0 at once; before memcpy is called as it may overwrite up to 3 of these bytes
      // NOLINTNEXTLINE
      memcpy(pNode->dat.val, val, valLen); // clang-tidy prefers memcpy_s, however there is no doubt that buffer bounds are respected here
      pNode->dat.valLen = valLen;
      return true;
    }
  }

  // no suitable memory allocated
  uint32_t alignedValCap = UINT32_C(0);
  uint8_t *const duplicate = pair_dup_(pNode->dat.key, pNode->dat.keyLen, val, valLen, &alignedValCap);
  if (duplicate == NULL)
    return false;

  pair_free_(pNode);
  if (val == NULL)
  {
    pNode->dat.key = duplicate;
    pNode->dat.valLen = UINT32_C(0);
    pNode->dat.val = NULL;
    return true;
  }

  pNode->dat.key = duplicate + alignedValCap + 4;
  pNode->dat.valLen = valLen;
  pNode->dat.val = duplicate;
  pNode->alignedValCap = alignedValCap;
  return true;
}

// At this point we get a link to a stack from zero to just a few nodes and we figure out whether the key is contained.
// Cheap integer comparisons are performed first, a binary comparison should be done at most once in a well behaved hash map.
HM_PRIVATE node_t *search_(const hmc_t hm, const void *const key, const uint32_t keyLen, const uint64_t hash, uint32_t nodeIdx)
{
  while (nodeIdx != 0U) // check whether a stack of one or more nodes is linked; if so, iterate over it
  {
    node_t *const pNode = hm->pNodes + nodeIdx - 1;
    if (pNode->hash == hash && // check whether the key has the same hash
        pNode->dat.keyLen == keyLen && // if so, check whether the length of the found key fits
        hm->compFunc(pNode->dat.key, key, keyLen)) // if so, perform a binary comparison
      return pNode; // return the pointer to the belonging node only if the latter has proved equality

    nodeIdx = pNode->nextIdx;
  }

  return NULL;
}

// Same like `search_()` unless we also get the pointer to the previous chain link.
HM_PRIVATE node_t *search_get_prev_link_(const hmc_t hm, const void *const key, const uint32_t keyLen, const uint64_t hash, uint32_t **const ppNodeIdx)
{
  for (uint32_t *pNodeIdx = *ppNodeIdx; *pNodeIdx != 0U;)
  {
    node_t *const pNode = hm->pNodes + *pNodeIdx - 1;
    if (pNode->hash == hash && pNode->dat.keyLen == keyLen && hm->compFunc(pNode->dat.key, key, keyLen))
      return pNode;

    pNodeIdx = &(pNode->nextIdx);
    *ppNodeIdx = pNodeIdx; // via `ppNodeIdx` we carry the pointer to the previous chain link, which needs to get updated in `detach_()` and `hm_merge()`
  }

  return NULL;
}

// Check if we can do something to make iterations faster again.
HM_PRIVATE void optimize_(const hm_t hm)
{
  // an empty map does not need the stack for removed nodes any longer
  if (hm->nodesCnt == 0U)
  {
    hm->recyclingBucket = UINT32_C(0);
    hm->lastUsed = UINT32_C(0);
  }

  // if the hash map size goes below 8% of capacity, we consider shrinking it
  if (((uint64_t)(hm->nodesCnt) << 3U) / hm->nodesCap == 0U)
    hm_shrink(hm);
}

// Detach the value (that is, transfer the ownership to the caller), hand the node over for recycling.
HM_PRIVATE bool detach_(const hm_t hm, const void *const key, const uint32_t keyLen, void **const pVal, size_t *const pValLen)
{
  const uint64_t hash = hm->hashFunc(key, keyLen, hm->hashSeed);
  uint32_t *pPrev = hm->pBuckets + (hash & (uint64_t)(hm->bucketsMaxIdx));
  node_t *const pNode = search_get_prev_link_(hm, key, keyLen, hash, &pPrev);
  if (pNode == NULL)
    return false;

  *pPrev = pNode->nextIdx;
  if (pNode->dat.val == NULL)
    free((void *)(intptr_t)(pNode->dat.key));

  pNode->dat.key = NULL; // critical as this NULL separates removed from still used nodes
  pNode->nextIdx = hm->recyclingBucket;
  hm->recyclingBucket = (uint32_t)(pNode - hm->pNodes + 1);
  --hm->nodesCnt;
  if (pValLen != NULL)
    *pValLen = pNode->dat.valLen;

  *pVal = pNode->dat.val;
  optimize_(hm);
  return true;
}

// Recreate the map data in smaller arrays as a subtask of `hm_shrink()`.
HM_PRIVATE void copy_items_(const hmc_t hm, uint32_t *const pBuckets, const uint32_t bucketsMaxIdx, node_t *const pNodes)
{
  node_t *newIt = pNodes;
  uint32_t idx = UINT32_C(1); // actual index in pNodes + 1
  for (const node_t *oldIt = hm->pNodes, *const end = hm->pNodes + hm->lastUsed; oldIt < end; ++oldIt)
  {
    if (oldIt->dat.key == NULL)
      continue;

    *newIt = *oldIt;
    uint32_t *const pBucket = pBuckets + (oldIt->hash & (uint64_t)bucketsMaxIdx);
    newIt->nextIdx = *pBucket;
    *pBucket = idx;
    ++idx;
    ++newIt;
  }
}

// Recreate the stacks of used nodes as a subtask of `increase_()`.
HM_PRIVATE void recreate_buckets_(uint32_t *const pBuckets, const uint32_t bucketsMaxIdx, node_t *const pNodes, const uint32_t lastUsed)
{
  uint32_t idx = UINT32_C(1); // actual index in pNodes + 1
  for (node_t *nodeIt = pNodes, *const end = nodeIt + lastUsed; nodeIt < end; ++nodeIt, ++idx)
  {
    if (nodeIt->dat.key == NULL)
      continue;

    uint32_t *const pBucket = pBuckets + (nodeIt->hash & (uint64_t)bucketsMaxIdx);
    nodeIt->nextIdx = *pBucket;
    *pBucket = idx;
  }
}

// Double the capacity of the hash map and recreate the stacks (update the indices in `pBuckets` and the `nextIdx` members).
HM_PRIVATE bool increase_(const hm_t hm)
{
  if (hm->bucketsMaxIdx == (UINT32_MAX >> 2U))
    return false;

  const uint32_t bucketsMaxIdx = (hm->bucketsMaxIdx << 1) + 1;
  uint32_t *const pBuckets = calloc((size_t)bucketsMaxIdx + 1, sizeof(uint32_t)); // zero-initialization is critical as zero values indicate that no stack is linked yet
  if (pBuckets == NULL)
    return false;

  const uint32_t nodesCap = hm->nodesCap << 1U;
  node_t *const pNodes = realloc(hm->pNodes, nodesCap * sizeof(node_t));
  if (pNodes == NULL)
  {
    free(pBuckets);
    return false;
  }

  free(hm->pBuckets);
  recreate_buckets_(pBuckets, bucketsMaxIdx, pNodes, hm->lastUsed);
  hm->pNodes = pNodes;
  hm->pBuckets = pBuckets;
  hm->nodesCap = nodesCap;
  hm->bucketsMaxIdx = bucketsMaxIdx;
  return true;
}

// Select an unused node and put it on top of the specified stack. Update hash map data that are unrelated to the value to be added.
HM_PRIVATE node_t *new_stacked_node_(const hm_t hm, uint32_t *const pBucket)
{
  if (hm->recyclingBucket == 0U) // no removed node, so take a new unused node
  {
    node_t *const pNode = hm->pNodes + hm->lastUsed; // pointer to the next free node
    pNode->nextIdx = *pBucket;
    *pBucket = ++hm->lastUsed;
    return pNode;
  }

  // reuse a node that has been removed
  node_t *const pNode = hm->pNodes + hm->recyclingBucket - 1; // pointer to the latest removed node
  const uint32_t nextRecycled = pNode->nextIdx;
  pNode->nextIdx = *pBucket;
  *pBucket = hm->recyclingBucket;
  hm->recyclingBucket = nextRecycled;
  return pNode;
}

// Try to move a source node into the destination map.
HM_PRIVATE bool merge_node_(const hm_t dest, const hm_t src, node_t *const pSrcNode, const bool doRehash, const bool updateExisting)
{
  const uint64_t destHash = doRehash ? dest->hashFunc(pSrcNode->dat.key, pSrcNode->dat.keyLen, dest->hashSeed) : pSrcNode->hash;
  uint32_t *pDstBucket = dest->pBuckets + (destHash & (uint64_t)(dest->bucketsMaxIdx));
  node_t *pDestNode = search_(dest, pSrcNode->dat.key, pSrcNode->dat.keyLen, destHash, *pDstBucket);
  if (pDestNode != NULL) // key exists in destination
  {
    if (!updateExisting)
      return true;

    pair_free_(pDestNode);
  }
  else // we need a new destination node
  {
    if (dest->nodesCnt == dest->nodesCap) // the capacity of destination needs to be increased
    {
      if (!increase_(dest))
        return false; // memory allocation failed

      pDstBucket = dest->pBuckets + (destHash & (uint64_t)(dest->bucketsMaxIdx)); // critical because the old `pDstBucket` is a dangling pointer now
    }

    pDestNode = new_stacked_node_(dest, pDstBucket);
    ++dest->nodesCnt;
  }

  // move the source data into the node of the destination, hand the source node over for recycling
  uint32_t *pSrcPrev = src->pBuckets + (pSrcNode->hash & (uint64_t)(src->bucketsMaxIdx));
  search_get_prev_link_(src, pSrcNode->dat.key, pSrcNode->dat.keyLen, pSrcNode->hash, &pSrcPrev);
  *pSrcPrev = pSrcNode->nextIdx;
  pDestNode->hash = destHash;
  pDestNode->dat = pSrcNode->dat;
  pDestNode->alignedValCap = pSrcNode->alignedValCap;
  pSrcNode->dat.key = NULL; // critical as this NULL separates removed from still used nodes
  pSrcNode->nextIdx = src->recyclingBucket;
  src->recyclingBucket = (uint32_t)(pSrcNode - src->pNodes + 1);
  --src->nodesCnt;
  return true;
}

// Add key and value to the hash map. Relies on previous checks being performed.
HM_PRIVATE bool add_new_(const hm_t hm, const void *const key, const uint32_t keyLen, const void *const val, const uint32_t valLen, const uint64_t hash, uint32_t *pBucket)
{
  if (hm->nodesCnt == hm->nodesCap) // the capacity needs to be increased
  {
    if (!increase_(hm))
      return false; // memory allocation failed

    pBucket = hm->pBuckets + (hash & (uint64_t)(hm->bucketsMaxIdx)); // critical because the old `pBucket` is a dangling pointer now
  }

  uint32_t alignedValCap = UINT32_C(0);
  uint8_t *const duplicate = pair_dup_(key, keyLen, val, valLen, &alignedValCap);
  if (duplicate == NULL)
    return false;

  node_t *const pNode = new_stacked_node_(hm, pBucket);
  if (val == NULL)
  {
    pNode->dat.key = duplicate;
    pNode->dat.valLen = UINT32_C(0);
    pNode->dat.val = NULL;
  }
  else
  {
    pNode->dat.key = duplicate + alignedValCap + 4;
    pNode->dat.valLen = valLen;
    pNode->dat.val = duplicate;
    pNode->alignedValCap = alignedValCap;
  }

  pNode->dat.keyLen = keyLen;
  pNode->hash = hash;
  ++hm->nodesCnt;
  return true;
}

// Create an empty hash map with a certain capacity.
HM_PRIVATE hm_t create_(hash_func_t hashFunc, const uint64_t hashSeed, equ_comp_t compFunc, const uint32_t nodesCap, const uint32_t bucketsMaxIdx)
{
  hm_t hm = calloc(1, sizeof(struct hm_spec));
  if (hm == NULL)
    return NULL;

  hm->pNodes = malloc(sizeof(node_t) * nodesCap);
  if (hm->pNodes == NULL)
  {
    free(hm);
    return NULL;
  }

  hm->pBuckets = calloc((size_t)bucketsMaxIdx + 1, sizeof(uint32_t)); // zero-initialization is critical as zero values indicate that no stack is linked yet
  if (hm->pBuckets == NULL)
  {
    free(hm->pNodes);
    free(hm);
    return NULL;
  }

  if (hashFunc != NULL)
  {
    hm->hashSeed = hashSeed;
    hm->hashFunc = hashFunc;
  }
  else
    hm->hashFunc = &get_hash_;

  hm->compFunc = compFunc == NULL ? &keys_equal_ : compFunc;
  hm->nodesCap = nodesCap;
  hm->bucketsMaxIdx = bucketsMaxIdx;
  return hm;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~ hash map interface ~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

HM_NODISCARD hm_t hm_create(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc)
{
  return create_(hashFunc, hashSeed, compFunc, MIN_NODES_CAP, MIN_BUCKETS_CAP - 1);
}

HM_NODISCARD hm_t hm_create_capacity(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc, size_t cap)
{
  if (cap <= MIN_NODES_CAP)
    return hm_create(hashFunc, hashSeed, compFunc);

  uint32_t nodesCap = MIN_NODES_CAP << 1U;
  uint32_t bucketsCap = MIN_BUCKETS_CAP << 1U;
  for (; nodesCap < cap && bucketsCap < (UINT32_MAX >> 2U); nodesCap <<= 1U, bucketsCap <<= 1U)
    ;

  return nodesCap < cap ? NULL : create_(hashFunc, hashSeed, compFunc, nodesCap, bucketsCap - 1);
}

int hm_add(hm_t hm, const void *key, size_t keyLen, const void *val, size_t valLen)
{
  if (keyLen > (UINT32_MAX >> 1U) || valLen > (UINT32_MAX >> 1U))
    return 0;

  const uint64_t hash = hm->hashFunc(key, keyLen, hm->hashSeed);
  uint32_t *const pBucket = hm->pBuckets + (hash & (uint64_t)(hm->bucketsMaxIdx)); // pointer to the bucket belonging to the hash
  return search_(hm, key, (uint32_t)keyLen, hash, *pBucket) == NULL ?
           add_new_(hm, key, (uint32_t)keyLen, val, (uint32_t)valLen, hash, pBucket) != false : // yields 1 if the item was added, 0 otherwise
           -1; // the key does already exist
}

bool hm_update(hm_t hm, const void *key, size_t keyLen, const void *val, size_t valLen)
{
  if (keyLen > (UINT32_MAX >> 1U) || valLen > (UINT32_MAX >> 1U))
    return false;

  const uint64_t hash = hm->hashFunc(key, keyLen, hm->hashSeed);
  uint32_t *const pBucket = hm->pBuckets + (hash & (uint64_t)(hm->bucketsMaxIdx)); // pointer to the bucket belonging to the hash
  node_t *const pNode = search_(hm, key, (uint32_t)keyLen, hash, *pBucket);
  return pNode != NULL ?
           assign_dat_(pNode, val, (uint32_t)valLen) :
           add_new_(hm, key, (uint32_t)keyLen, val, (uint32_t)valLen, hash, pBucket);
}

bool hm_merge(hm_t dest, hm_t src, bool updateExisting)
{
  if (src->nodesCnt == 0U)
    return true; // source is empty

  const bool doRehash = dest->hashFunc != src->hashFunc || dest->hashSeed != src->hashSeed; // we can only reuse the source hash if both the same hashing function and seed have been used
  for (node_t *srcIt = src->pNodes, *const end = src->pNodes + src->lastUsed; srcIt < end; ++srcIt)
  {
    if (srcIt->dat.key == NULL)
      continue; // this is a removed node in source

    if (!merge_node_(dest, src, srcIt, doRehash, updateExisting))
      return false;
  }

  optimize_(src);
  return true;
}

HM_NODISCARD void *hm_detach(hm_t hm, const void *key, size_t keyLen, size_t *pValLen)
{
  void *val;
  return keyLen > (UINT32_MAX >> 1U) || !detach_(hm, key, (uint32_t)keyLen, &val, pValLen) ? NULL : val;
}

bool hm_remove(hm_t hm, const void *key, size_t keyLen)
{
  void *val;
  if (keyLen > (UINT32_MAX >> 1U) || !detach_(hm, key, (uint32_t)keyLen, &val, NULL))
    return false;

  free(val);
  return true;
}

bool hm_contains(hmc_t hm, const void *key, size_t keyLen)
{
  if (keyLen > (UINT32_MAX >> 1U))
    return false;

  const uint64_t hash = hm->hashFunc(key, keyLen, hm->hashSeed);
  return search_(hm, key, (uint32_t)keyLen, hash, hm->pBuckets[hash & (uint64_t)(hm->bucketsMaxIdx)]) != NULL;
}

hm_iter_t hm_item(hmc_t hm, const void *key, size_t keyLen)
{
  if (keyLen > (UINT32_MAX >> 1U))
    return NULL;

  const uint64_t hash = hm->hashFunc(key, keyLen, hm->hashSeed);
  const node_t *const pNode = search_(hm, key, (uint32_t)keyLen, hash, hm->pBuckets[hash & (uint64_t)(hm->bucketsMaxIdx)]);
  return pNode == NULL ? NULL : &(pNode->dat);
}

hm_iter_t hm_next(hmc_t hm, hm_iter_t current)
{
  for (const node_t *nodeIt = (current != NULL ? (const node_t *)current + 1 : hm->pNodes), *const end = hm->pNodes + hm->lastUsed; nodeIt < end; ++nodeIt)
    if (nodeIt->dat.key != NULL)
      return &(nodeIt->dat);

  return NULL;
}

hm_iter_t hm_prev(hmc_t hm, hm_iter_t current)
{
  for (const node_t *rNodeIt = (current != NULL ? (const node_t *)current : hm->pNodes + hm->lastUsed), *const rEnd = hm->pNodes; rNodeIt > rEnd;)
  {
    --rNodeIt;
    if (rNodeIt->dat.key != NULL)
      return &(rNodeIt->dat);
  }

  return NULL;
}

bool hm_empty(hmc_t hm)
{
  return hm->nodesCnt == 0U;
}

size_t hm_length(hmc_t hm)
{
  return hm->nodesCnt;
}

size_t hm_capacity(hmc_t hm)
{
  return hm->nodesCap;
}

void hm_free_detached(const void *detachedPtr)
{
  free((void *)(intptr_t)detachedPtr);
}

bool hm_shrink(hm_t hm)
{
  uint32_t nodesCap = MIN_NODES_CAP;
  uint32_t bucketsCap = MIN_BUCKETS_CAP;
  for (; nodesCap < hm->nodesCnt; nodesCap <<= 1U, bucketsCap <<= 1U)
    ;

  if (nodesCap == hm->nodesCap)
    return true;

  node_t *const pNodes = malloc(sizeof(node_t) * nodesCap);
  if (pNodes == NULL)
    return false;

  uint32_t *const pBuckets = calloc(bucketsCap, sizeof(uint32_t)); // zero-initialization is critical as zero values indicate that no stack is linked yet
  if (pBuckets == NULL)
  {
    free(pNodes);
    return false;
  }

  if (hm->nodesCnt != 0U)
    copy_items_(hm, pBuckets, bucketsCap - 1, pNodes);

  free(hm->pNodes);
  free(hm->pBuckets);
  hm->pNodes = pNodes;
  hm->pBuckets = pBuckets;
  hm->nodesCap = nodesCap;
  hm->bucketsMaxIdx = bucketsCap - 1;
  hm->recyclingBucket = UINT32_C(0);
  hm->lastUsed = hm->nodesCnt;
  return true;
}

void hm_clear(hm_t hm)
{
  if (hm->nodesCnt == 0U)
    return;

  destroy_values_(hm);
  if (hm->nodesCap == MIN_NODES_CAP) // otherwise optimize_() will allocate new arrays via hm_shrink() anyway
    // NOLINTNEXTLINE
    memset(hm->pBuckets, 0, sizeof(uint32_t) * hm->bucketsMaxIdx + 1); // clang-tidy prefers memset_s; however, neither do we violate buffer bounds nor can the compiler skip performing the memset

  hm->nodesCnt = UINT32_C(0);
  optimize_(hm); // it sets recyclingBucket and lastUsed to 0 for us, among other things
}

void hm_destroy(hmc_t hm)
{
  if (hm->nodesCnt != 0U)
    destroy_values_(hm);

  free(hm->pBuckets);
  free(hm->pNodes);
  free((void *)(intptr_t)hm);
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~ hash set interface ~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// clang-format off

// Interface-only structure wrapping the hash map structure to get the same size and alignment for less noises in type casts.
struct hs_spec
{
    struct hm_spec  unused_;
};

// clang-format on

HS_NODISCARD hs_t hs_create(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc)
{
  return (hs_t)hm_create(hashFunc, hashSeed, compFunc);
}

HS_NODISCARD hs_t hs_create_capacity(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc, size_t cap)
{
  return (hs_t)hm_create_capacity(hashFunc, hashSeed, compFunc, cap);
}

int hs_add(hs_t hs, const void *val, size_t len)
{
  return hm_add((hm_t)hs, val, len, NULL, UINT32_C(0)); // in a hash set, the key is also the value, so all value fields of the wrapped hash map are NULL
}

bool hs_merge(hs_t dest, hs_t src)
{
  return hm_merge((hm_t)dest, (hm_t)src, false); // in a hash set we have no value to update, so the last parameter is always `false`
}

bool hs_remove(hs_t hs, const void *val, size_t len)
{
  return hm_remove((hm_t)hs, val, len);
}

bool hs_contains(hsc_t hs, const void *val, size_t len)
{
  return hm_contains((hmc_t)hs, val, len);
}

hs_iter_t hs_item(hsc_t hs, const void *val, size_t len)
{
  return (hs_iter_t)hm_item((hmc_t)hs, val, len);
}

hs_iter_t hs_next(hsc_t hs, hs_iter_t current)
{
  return (hs_iter_t)hm_next((hmc_t)hs, (hm_iter_t)current);
}

hs_iter_t hs_prev(hsc_t hs, hs_iter_t current)
{
  return (hs_iter_t)hm_prev((hmc_t)hs, (hm_iter_t)current);
}

bool hs_empty(hsc_t hs)
{
  return hm_empty((hmc_t)hs);
}

size_t hs_length(hsc_t hs)
{
  return hm_length((hmc_t)hs);
}

size_t hs_capacity(hsc_t hs)
{
  return hm_capacity((hmc_t)hs);
}

bool hs_shrink(hs_t hs)
{
  return hm_shrink((hm_t)hs);
}

void hs_clear(hs_t hs)
{
  hm_clear((hm_t)hs);
}

void hs_destroy(hsc_t hs)
{
  hm_destroy((hmc_t)hs);
}

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
