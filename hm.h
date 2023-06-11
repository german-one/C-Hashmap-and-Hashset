/// @copyright Copyright (c) 2023 Steffen Illhardt,
///            Licensed under the MIT license
///            ( https://opensource.org/license/mit/ ).
/// @file      hm.h
/// @brief     C interface of a hash map, incl. an implementation of a hash set.
/// @version   1.0
/// @author    Steffen Illhardt
/// @date      2023
/// @pre       Requires compiler support for at least C99.

#ifndef HASHMAP_12AA98F5_9135_48EA_9AD3_8619146FAEAE
/// @cond HASHMAP_12AA98F5_9135_48EA_9AD3_8619146FAEAE
#define HASHMAP_12AA98F5_9135_48EA_9AD3_8619146FAEAE
/// @endcond

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpadded"
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4820) // padding added
#endif

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @defgroup hash_func   Hashing Function Interface
/// Pointer specification of a custom function to calculate hash values.
/// @{

// clang-format off

/// @brief Type `hash_func_t` specifies a pointer to a custom hashing function
///        used to calculate the hash values in a hash map or hash set.
/// @param data      Pointer to the first byte of the data.
/// @param dataLen   Length of the data as number of bytes.
/// @param hashSeed  Value used to randomize the hash function.
/// @return Hash value as 64-bit unsigned integer.
typedef  uint64_t ( *hash_func_t )(const void *data, size_t dataLen, uint64_t hashSeed);

// clang-format on

/// @} // hash_func end
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @defgroup comp_func   Comparison Function Interface
/// Pointer specification of a custom function to compare two key values for
/// equality.
/// @{

// clang-format off

/// @brief Type `equ_comp_t` specifies a pointer to a custom comparison function
///        used to determine the equality of two key values with both having
///        the same length.
/// @param key1     Pointer to the first byte of the first key.
/// @param key2     Pointer to the first byte of the second key.
/// @param keyLen   Common length of the keys as number of bytes.
/// @return `true` if the keys are equal, `false` otherwise.
typedef  bool ( *equ_comp_t )(const void *key1, const void *key2, size_t keyLen);

// clang-format on

/// @} // comp_func end
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @defgroup hash_map   Hash Map Interface
/// A hash map is an unordered container to store items as key-value pairs
/// where the keys are unique.
/// @{

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
#  define HM_NODISCARD [[nodiscard]] ///< Do not discard the returned value!
#elif defined(__GNUC__) || defined(__clang__)
#  define HM_NODISCARD __attribute__((__warn_unused_result__)) ///< Do not discard the returned value!
#elif defined(_MSC_VER) && (_MSC_VER >= 1700)
#  define HM_NODISCARD _Check_return_ ///< Do not discard the returned value!
#else
#  define HM_NODISCARD ///< Do not discard the returned value!
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define HM_NONNULL(_idx) __attribute__((__nonnull__(_idx))) ///< Do not pass a NULL pointer argument to the specified parameter!
#else
#  define HM_NONNULL(_idx) ///< Do not pass a NULL pointer argument to the specified parameter!
#endif

// clang-format off

/// @brief The pointer type `hm_t` represents a handle to the opaque hash map
///        structure.
typedef        struct hm_spec  *hm_t;

/// @brief The pointer type `hmc_t` represents a read-only handle to the opaque
///        hash map structure.
typedef  const struct hm_spec  *hmc_t;

/// @brief Structure which contains the data of a hash map item and the lengths
///        as numbers of bytes. (The slightly counter-intuitive order of members
///        is critical for the integration of the hash set interface.) <br>
///        The pointer type `hm_iter_t` serves as an iterator-like type.
typedef  const struct hm_item_spec
{
    /// Pointer to the first byte of the key. The key is always appended with
    /// null bytes, enough to serve as the terminator for any string type. <br>
    /// NULL pointer not allowed.
    const void  *key;
    /// Length of the key as number of bytes. Terminating null character not
    /// counted in string data.
    uint32_t     keyLen;
    /// Length of the value as number of bytes. Terminating null character not
    /// counted in string data. <br>
    /// If `val` is a NULL pointer, this member is set to 0.
    uint32_t     valLen;
    /// Pointer to the first byte of the value. The value is always appended
    /// with null bytes, enough to serve as the terminator for any string
    /// type. <br>
    /// NULL pointer allowed.
    void        *val;
}  *hm_iter_t;

// clang-format on

/// @brief Allocate and initialize resources for an empty hash map. The original
///        capacity is 192.
/// @param hashFunc  Function used to calculate the hash values of items in the
///                  hash map. <br>
///                  If a NULL pointer is passed, FNV-1a is used as a very
///                  simple but unsafe hashing algorithm.
/// @param hashSeed  Value used to randomize the hash function. <br>
///                  If `hashFunc` is `NULL`, this parameter is ignored.
/// @param compFunc  Function used used to determine the equality of two keys
///                  with both having the same length. <br>
///                  If a NULL pointer is passed, `memcmp()` is used.
/// @return Handle to the newly created hash map, `NULL` if the allocation of
///         resources failed. <br>
///         Release allocated resources using `hm_destroy()` if the hash map is
///         not used any longer.
HM_NODISCARD hm_t hm_create(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc);

/// @brief Allocate and initialize resources for an empty hash map with the
///        specified minimum capacity.
/// @param hashFunc  Function used to calculate the hash values of items in the
///                  hash map. <br>
///                  If a NULL pointer is passed, FNV-1a is used as a very
///                  simple but unsafe hashing algorithm.
/// @param hashSeed  Value used to randomize the hash function. <br>
///                  If `hashFunc` is `NULL`, this parameter is ignored.
/// @param compFunc  Function used used to determine the equality of two keys
///                  with both having the same length. <br>
///                  If a NULL pointer is passed, `memcmp()` is used.
/// @param cap       Minimum capacity of the hash map. Values <= 192 result in
///                  an initial capacity of 192.
/// @return Handle to the newly created hash map, `NULL` if the allocation of
///         resources failed. <br>
///         Release allocated resources using `hm_destroy()` if the hash map is
///         not used any longer.
HM_NODISCARD hm_t hm_create_capacity(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc, size_t cap);

/// @brief Add an item to the hash map if the key does not exist. Reject the
///        data otherwise. Comparison with existing keys is case-sensitive if
///        both the default hasher and default comparer are used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm      Handle to the hash map.
/// @param key     Pointer to the first byte of the key to be added. <br>
///                The key is copied and automatically appended with null bytes,
///                enough to serve as the terminator for any string type. <br>
///                NULL pointer not allowed.
/// @param keyLen  Length (as number of bytes) of the key to be added.
///                Terminating null character not counted (if any).
/// @param val     Pointer to the first byte of the value to be added. <br>
///                The value is copied and automatically appended with zero
///                bytes, enough to serve as the terminator for any string
///                type. <br>
///                NULL pointer allowed.
/// @param valLen  Length (as number of bytes) of the value to be added.
///                Terminating null character not counted (if any). <br>
///                This parameter is ignored if `val` is a NULL pointer.
/// @return  1 if the item is added to the hash map, <br>
///         -1 if the data is rejected, <br>
///          0 fatal error (e.g. memory allocation failed), leaving the hash map
///            unchanged in a viable condition.
int hm_add(hm_t hm, const void *key, size_t keyLen, const void *val, size_t valLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Add the item to the hash map if the key does not exist, or replace
///        the old value if the key already exists. Comparison with existing
///        keys is case-sensitive if both the default hasher and default
///        comparer are used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm      Handle to the hash map.
/// @param key     Pointer to the first byte of the key. <br>
///                If the key does not exist, it is copied and automatically
///                appended with null bytes, enough to serve as the terminator
///                for any string type. <br>
///                NULL pointer not allowed.
/// @param keyLen  Length (as number of bytes) of the key. Terminating null
///                character not counted (if any).
/// @param val     Pointer to the first byte of the value. If the key does not
///                exist, the item is added to the hash map. <br>
///                If the key exists, the old value is replaced. <br>
///                The value is copied and automatically appended with zero
///                bytes, enough to serve as the terminator for any string
///                type. <br>
///                NULL pointer allowed.
/// @param valLen  Length (as number of bytes) of the value. Terminating null
///                character not counted (if any). <br>
///                This parameter is ignored if `val` is a NULL pointer.
/// @return `true`  if the hash map is updated successfully, <br>
///         `false` if memory allocation failed (fatal error), leaving the hash
///                 map unchanged in a viable condition.
bool hm_update(hm_t hm, const void *key, size_t keyLen, const void *val, size_t valLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Merge items of one hash map into another. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param dest  Handle to the destination hash map.
/// @param src   Handle to the source hash map.
/// @param updateExisting
///              - If `false`, only items of the source hash map with keys that
///                don't exist in the destination hash map are moved into the
///                destination hash map. Items with existing keys remain in the
///                source hash map. <br>
///              - If `true`, all items of the source hash map are moved into
///                the destination hash map. Items with existing keys in the
///                destination hash map are overwritten with the source data.
///                The source hash map is emptied.
/// @return `true`  if no error occurred, <br>
///         `false` if memory allocation failed (fatal error), leaving the hash
///                 maps in viable conditions, possibly partially merged.
bool hm_merge(hm_t dest, hm_t src, bool updateExisting)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Try to remove an item from the hash map. Return the pointer to the
///        removed value. Comparison with existing keys is case-sensitive if
///        both the default hasher and default comparer are used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm       Handle to the hash map.
/// @param key      Pointer to the first byte of the key to be compared.
/// @param keyLen   Length (as number of bytes) of the key to be compared.
///                 Terminating null character not counted (if any).
/// @param pValLen  Pointer to an object that receives the length of the
///                 returned value. <br>
///                 NULL pointer allowed.
/// @return Pointer to the detached value. <br>
///         `NULL` is returned if the hash map is empty, if the key is not
///         found, or if the value is `NULL`. <br>
///         NOTE: The caller obtains ownership of the value and is responsible
///         for freeing any non-NULL pointers returned. Deallocate these
///         pointers using `hm_free_detached()` if they are not used any longer.
HM_NODISCARD void *hm_detach(hm_t hm, const void *key, size_t keyLen, size_t *pValLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Try to remove an item from the hash map. Comparison with existing
///        keys is case-sensitive if both the default hasher and default
///        comparer are used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm      Handle to the hash map.
/// @param key     Pointer to the first byte of the key to be removed.
/// @param keyLen  Length (as number of bytes) of the key to be removed.
///                Terminating null character not counted (if any).
/// @return `true`  if the item is removed from the hash map, <br>
///         `false` if the item does not exist.
bool hm_remove(hm_t hm, const void *key, size_t keyLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Check whether or not the hash map contains the specified key.
///        Comparison of keys is case-sensitive if both the default hasher and
///        default comparer are used.
/// @param hm      Handle to the hash map.
/// @param key     Pointer to the first byte of the key to be compared.
/// @param keyLen  Length (as number of bytes) of the key to be compared.
///                Terminating null character not counted (if any).
/// @return `true`  if the hash map contains the key, <br>
///         `false` otherwise.
bool hm_contains(hmc_t hm, const void *key, size_t keyLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Get the pointer to the item in the hash map with the specified key.
///        Comparison of keys is case-sensitive if both the default hasher and
///        default comparer are used.
/// @param hm      Handle to the hash map.
/// @param key     Pointer to the first byte of the key to be compared.
/// @param keyLen  Length (as number of bytes) of the key to be compared.
///                Terminating null character not counted (if any).
/// @return Pointer to the item in the hash map with the specified key. <br>
///         `NULL` is returned if the hash map is empty or if the key is not
///         found. <br>
///         NOTE: Accessed data should generally be considered read-only to
///         avoid hash map corruption. <br>
///         The only exception is updating the value that the `hm_iter_t.val`
///         member points to, typically to perform arithmetic operations on
///         existing numeric values to avoid multiple map accesses. However,
///         this is still unsafe: <br>
///         THE USER IS RESPONSIBLE TO ENSURE THAT THE VALUE UPDATED VIA THIS
///         POINTER INTERFACE HAS THE SAME SIZE AS THE ORIGINAL VALUE.
///         DO NOT DEALLOCATE THE POINTER OR UPDATE ANY OTHER MEMBER.
hm_iter_t hm_item(hmc_t hm, const void *key, size_t keyLen)
  HM_NONNULL(1) HM_NONNULL(2);

/// @brief Get the pointer to the next item in the hash map. <br>
///        Use this interface if copying content of the hash map into another
///        container is needed.
/// @param hm       Handle to the hash map.
/// @param current  Recent pointer previously returned by this function,
///                 `hm_prev()` or `hm_item()`. <br>
///                 Specify `NULL` to get the pointer to the first item.
/// @return Pointer to the next item in the hash map. <br>
///         `NULL` is returned if the hash map is empty or if no further item
///         is available. <br>
///         NOTE: Accessed data should generally be considered read-only to
///         avoid hash map corruption. <br>
///         The only exception is updating the value that the `hm_iter_t.val`
///         member points to, typically to perform arithmetic operations on
///         existing numeric values to avoid multiple map accesses. However,
///         this is still unsafe: <br>
///         THE USER IS RESPONSIBLE TO ENSURE THAT THE VALUE UPDATED VIA THIS
///         POINTER INTERFACE HAS THE SAME SIZE AS THE ORIGINAL VALUE.
///         DO NOT DEALLOCATE THE POINTER OR UPDATE ANY OTHER MEMBER.
hm_iter_t hm_next(hmc_t hm, hm_iter_t current)
  HM_NONNULL(1);

/// @brief Get the pointer to the previous item in the hash map. <br>
///        Use this interface if copying content of the hash map into another
///        container is needed.
/// @param hm       Handle to the hash map.
/// @param current  Recent pointer previously returned by this function,
///                 `hm_next()` or `hm_item()`. <br>
///                 Specify `NULL` to get the pointer to the last item.
/// @return Pointer to the previous item in the hash map. <br>
///         `NULL` is returned if the hash map is empty or if no further item
///         is available. <br>
///         NOTE: Accessed data should generally be considered read-only to
///         avoid hash map corruption. <br>
///         The only exception is updating the value that the `hm_iter_t.val`
///         member points to, typically to perform arithmetic operations on
///         existing numeric values to avoid multiple map accesses. However,
///         this is still unsafe: <br>
///         THE USER IS RESPONSIBLE TO ENSURE THAT THE VALUE UPDATED VIA THIS
///         POINTER INTERFACE HAS THE SAME SIZE AS THE ORIGINAL VALUE.
///         DO NOT DEALLOCATE THE POINTER OR UPDATE ANY OTHER MEMBER.
hm_iter_t hm_prev(hmc_t hm, hm_iter_t current)
  HM_NONNULL(1);

/// @brief Check if the hash map is empty.
/// @param hm  Handle to the hash map.
/// @return `true`  if the number of items is zero, <br>
///         `false` otherwise.
bool hm_empty(hmc_t hm)
  HM_NONNULL(1);

/// @brief Get the current number of items in the hash map.
/// @param hm  Handle to the hash map.
/// @return Current number of items in the hash map.
size_t hm_length(hmc_t hm)
  HM_NONNULL(1);

/// @brief Get the maximum number of items the hash map can currently contain
///        without increasing.
/// @param hm  Handle to the hash map.
/// @return Maximum number of items the hash map can currently contain without
///         increasing.
size_t hm_capacity(hmc_t hm)
  HM_NONNULL(1);

/// @brief Deallocate the pointer previously returned by `hm_detach()`.
/// @param detachedPtr  Pointer to be released.
void hm_free_detached(const void *detachedPtr);

/// @brief Shrink the capacity of the hash map to the next 3/4 of a power of two
///        that is neither less than 192 nor less than the current number of
///        items. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm  Handle to the hash map.
/// @return `true`  if no error occurred, <br>
///         `false` if memory allocation failed (fatal error), leaving the hash
///                 map unchanged in a viable condition.
bool hm_shrink(hm_t hm)
  HM_NONNULL(1);

/// @brief Deallocate item resources and reset the hash map to an empty
///        state. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hm_item()`, `hm_next()` or `hm_prev()`.
/// @param hm  Handle to the hash map.
void hm_clear(hm_t hm)
  HM_NONNULL(1);

/// @brief Deallocate all resources of a hash map.
/// @param hm  Handle to the hash map, previously returned by `hm_create()` or
///        `hm_create_capacity()`.
void hm_destroy(hmc_t hm)
  HM_NONNULL(1);

/// @} // hash_map end
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @defgroup hash_set   Hash Set Interface
/// A hash set is an unordered container to store unique values.
/// @{

#define HS_NODISCARD HM_NODISCARD ///< @copybrief HM_NODISCARD

#define HS_NONNULL(_idx) HM_NONNULL(_idx) ///< @copybrief HM_NONNULL

// clang-format off

/// @brief The pointer type `hs_t` represents a handle to the opaque hash set
///        structure.
typedef        struct hs_spec  *hs_t;

/// @brief The pointer type `hsc_t` represents a read-only handle to the opaque
///        hash set structure.
typedef  const struct hs_spec  *hsc_t;

/// @brief Structure which contains the value of a hash set item and its length
///        as number of bytes. (The members are aliases for `hm_item.key` and
///        `hm_item.keyLen` of the underlying hash map implementation.) <br>
///        The pointer type `hs_iter_t` serves as an iterator-like type to
///        read-only data.
typedef  const struct hs_item_spec
{
    /// Pointer to the first byte of the value. The value is always appended
    /// with null bytes, enough to serve as the terminator for any string
    /// type. <br>
    /// NULL pointer not allowed.
    const void  *val;
    /// Length of the value as number of bytes. Terminating null character not
    /// counted in string data.
    uint32_t     len;
}  *hs_iter_t;

// clang-format on

/// @brief Allocate and initialize resources for an empty hash set. The original
///        capacity is 192.
/// @param hashFunc  Function used to calculate the hash values of items in the
///                  hash set. <br>
///                  If a NULL pointer is passed, FNV-1a is used as a very
///                  simple but unsafe hashing algorithm.
/// @param hashSeed  Value used to randomize the hash function. <br>
///                  If `hashFunc` is `NULL`, this parameter is ignored.
/// @param compFunc  Function used used to determine the equality of two values
///                  with both having the same length. <br>
///                  If a NULL pointer is passed, `memcmp()` is used.
/// @return Handle to the newly created hash set, `NULL` if the allocation of
///         resources failed. <br>
///         Release allocated resources using `hs_destroy()` if the hash set is
///         not used any longer.
HS_NODISCARD hs_t hs_create(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc);

/// @brief Allocate and initialize resources for an empty hash set with the
///        specified minimum capacity.
/// @param hashFunc  Function used to calculate the hash values of items in the
///                  hash set. <br>
///                  If a NULL pointer is passed, FNV-1a is used as a very
///                  simple but unsafe hashing algorithm.
/// @param hashSeed  Value used to randomize the hash function. <br>
///                  If `hashFunc` is `NULL`, this parameter is ignored.
/// @param compFunc  Function used used to determine the equality of two values
///                  with both having the same length. <br>
///                  If a NULL pointer is passed, `memcmp()` is used.
/// @param cap       Minimum capacity of the hash set. Values <= 192 result in
///                  an initial capacity of 192.
/// @return Handle to the newly created hash set, `NULL` if the allocation of
///         resources failed. <br>
///         Release allocated resources using `hs_destroy()` if the hash set is
///         not used any longer.
HS_NODISCARD hs_t hs_create_capacity(hash_func_t hashFunc, uint64_t hashSeed, equ_comp_t compFunc, size_t cap);

/// @brief Add an item to the hash set if the value does not exist.
///        Reject the data otherwise. Comparison with existing values is
///        case-sensitive if both the default hasher and default comparer are
///        used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hs_item()`, `hs_next()` or `hs_prev()`.
/// @param hs   Handle to the hash set.
/// @param val  Pointer to the first byte of the string, substring or binary
///             data to be added. <br>
///             The value is copied and automatically appended with null bytes,
///             enough to serve as the terminator for any string type.
/// @param len  Length (as number of bytes) of the string, substring or binary
///             data to be added. Terminating null character not counted
///             (if any).
/// @return  1 if the item is added to the hash set, <br>
///         -1 if the data is rejected, <br>
///          0 fatal error (e.g. memory allocation failed), leaving the hash set
///            unchanged in a viable condition.
int hs_add(hs_t hs, const void *val, size_t len)
  HS_NONNULL(1) HS_NONNULL(2);

/// @brief Merge items of one hash set into another. <br>
///        Items of the source hash set with values that don't exist in the
///        destination hash set are moved into the destination hash set. <br>
///        Items with existing values remain in the source hash set. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hs_item()`, `hs_next()` or `hs_prev()`.
/// @param dest  Handle to the destination hash set.
/// @param src   Handle to the source hash set.
/// @return `true`  if no error occurred, <br>
///         `false` if memory allocation failed (fatal error), leaving the hash
///                 sets in viable conditions, possibly partially merged.
bool hs_merge(hs_t dest, hs_t src)
  HS_NONNULL(1) HS_NONNULL(2);

/// @brief Try to remove an item from the hash set. Comparison with existing
///        values is case-sensitive if both the default hasher and default
///        comparer are used. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hs_item()`, `hs_next()` or `hs_prev()`.
/// @param hs   Handle to the hash set.
/// @param val  Pointer to the first byte of the string or binary data to be
///             removed.
/// @param len  Length (as number of bytes) of the string or binary data to be
///             removed. Terminating null character not counted (if any).
/// @return `true`  if the item is removed from the hash set, <br>
///         `false` if the item does not exist.
bool hs_remove(hs_t hs, const void *val, size_t len)
  HS_NONNULL(1) HS_NONNULL(2);

/// @brief Check whether or not the hash set contains the specified item.
///        Comparison of values is case-sensitive if both the default hasher and
///        default comparer are used.
/// @param hs   Handle to the hash set.
/// @param val  Pointer to the first byte of the string or binary data to be
///             compared.
/// @param len  Length (as number of bytes) of the string or binary data to be
///             compared. Terminating null character not counted (if any).
/// @return `true`  if the hash set contains the item, <br>
///         `false` otherwise.
bool hs_contains(hsc_t hs, const void *val, size_t len)
  HS_NONNULL(1) HS_NONNULL(2);

/// @brief Get the pointer to the item in the hash set with the specified value.
///        Comparison of values is case-sensitive if both the default hasher and
///        default comparer are used.
/// @param hs   Handle to the hash set.
/// @param val  Pointer to the first byte of the string or binary data to be
///             compared.
/// @param len  Length (as number of bytes) of the string or binary data to be
///             compared. Terminating null character not counted (if any).
/// @return Pointer to the specified item in the hash set. <br>
///         `NULL` is returned if the hash set is empty or if the specified
///         item is not contained. <br>
///         NOTE: Do not update content via the iterator interface! Accessed
///         data must be considered read-only to avoid hash set corruption.
hs_iter_t hs_item(hsc_t hs, const void *val, size_t len)
  HS_NONNULL(1) HS_NONNULL(2);

/// @brief Get the pointer to the next item in the hash set. <br>
///        Use this interface if copying content of the hash set into another
///        container is needed.
/// @param hs       Handle to the hash set.
/// @param current  Recent pointer previously returned by this function,
///                 `hs_prev()` or `hs_item()`. <br>
///                 Specify `NULL` to get the pointer to the first item.
/// @return Pointer to the next item in the hash set. <br>
///         `NULL` is returned if the hash set is empty or if no further item
///         is available. <br>
///         NOTE: Do not update content via the iterator interface! Accessed
///         data must be considered read-only to avoid hash set corruption.
hs_iter_t hs_next(hsc_t hs, hs_iter_t current)
  HS_NONNULL(1);

/// @brief Get the pointer to the previous item in the hash set. <br>
///        Use this interface if copying content of the hash set into another
///        container is needed.
/// @param hs       Handle to the hash set.
/// @param current  Recent pointer previously returned by this function,
///                 `hs_next()` or `hs_item()`. <br>
///                 Specify `NULL` to get the pointer to the last item.
/// @return Pointer to the previous item in the hash set. <br>
///         `NULL` is returned if the hash set is empty or if no further item
///         is available. <br>
///         NOTE: Do not update content via the iterator interface! Accessed
///         data must be considered read-only to avoid hash set corruption.
hs_iter_t hs_prev(hsc_t hs, hs_iter_t current)
  HS_NONNULL(1);

/// @brief Check if the hash set is empty.
/// @param hs  Handle to the hash set.
/// @return `true`  if the number of items is zero, <br>
///         `false` otherwise.
bool hs_empty(hsc_t hs)
  HS_NONNULL(1);

/// @brief Get the current number of items in the hash set.
/// @param hs  Handle to the hash set.
/// @return Current number of items in the hash set.
size_t hs_length(hsc_t hs)
  HS_NONNULL(1);

/// @brief Get the maximum number of items the hash set can currently contain
///        without increasing.
/// @param hs  Handle to the hash set.
/// @return Maximum number of items the hash set can currently contain without
///         increasing.
size_t hs_capacity(hsc_t hs)
  HS_NONNULL(1);

/// @brief Shrink the capacity of the hash set to the next 3/4 of a power of two
///        that is neither less than 192 nor less than the current number of
///        items. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hs_item()`, `hs_next()` or `hs_prev()`.
/// @param hs  Handle to the hash set.
/// @return `true`  if no error occurred, <br>
///         `false` if memory allocation failed (fatal error), leaving the hash
///                 set unchanged in a viable condition.
bool hs_shrink(hs_t hs)
  HS_NONNULL(1);

/// @brief Deallocate item resources and reset the hash set to an empty
///        state. <br>
///        NOTE: This function invalidates pointers previously returned by
///        `hs_item()`, `hs_next()` or `hs_prev()`.
/// @param hs  Handle to the hash set.
void hs_clear(hs_t hs)
  HS_NONNULL(1);

/// @brief Deallocate all resources of a hash set.
/// @param hs  Handle to the hash set, previously returned by `hs_create()` or
///        `hs_create_capacity()`.
void hs_destroy(hsc_t hs)
  HS_NONNULL(1);

/// @} // hash_set end
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @mainpage Introduction
///
/// @copydoc hm.h
///
/// <hr>
///
/// The file `hm.h` contains the `C` interface of both a Hash Map and a Hash Set
/// library, implemented in the `hm.c` source file.
///
/// Adding to the container copies both keys and values and automatically
/// appends null bytes, sufficient to serve as terminators for any string type,
/// if necessary. Of course, the interfaces can also be used for binary
/// data. <br>
/// The length of keys and values is defined at the element level to allow for
/// dynamically sized strings, arrays, or serialized data.
///
/// <hr>
///
/// - A Hash Map (a.k.a. Hash Table) is an unordered container to store items as
/// key-value pairs where the keys are unique. <br>
/// This implementation allows NULL pointer values. <br>
/// Values can be detached from the map to possibly avoid copying them once
/// again. <br>
/// For a detailed description refer to the @ref hash_map "Hash Map Interface"
/// module. <br><br>
///
/// - A Hash Set is an unordered container to store unique values. <br>
/// Since a Hash Set can be treated as a Hash Map where the key is also the
/// value and with the value field of the map set to null, this implementation
/// of a Hash Set just wraps the Hash Map interface. <br>
/// For a detailed description refer to the @ref hash_set "Hash Set Interface"
/// module. <br><br>
///
/// - The @ref hash_func "Hashing Function Interface" defines the pointer type
/// of a custom hashing function. It's shared with both the Hash Map and the
/// Hash Set interfaces. <br>
/// The aim of the function is to calculate hash values for keys of the Hash Map
/// or for values of the Hash Set, respectively. If they contain paddings with
/// undefined content (e.g. members of structures might be padded) you should
/// use a suitable algorithm for the calculation. Also, a case-insensitive
/// equality evaluation can be achieved using custom hashing and comparison
/// functions.<br>
/// Furthermore, there are many personal preferences for hashing algorithms.
/// Most of those should be able to get wrapped in a function with the
/// specification defined. <br>
/// If this interface is not used, the calculation of hash values will fall back
/// to a simple `FNV-1a` algorithm. <br><br>
///
/// - The @ref comp_func "Comparison Function Interface" defines the pointer
/// type of a custom equality comparison function. It's shared with both the
/// Hash Map and the Hash Set interfaces. <br>
/// The aim of the function is to compare keys of the Hash Map or values of the
/// Hash Set, respectively. If they contain paddings with undefined content
/// (e.g. members of structures might be padded) you should use a suitable
/// algorithm for the comparison. Also, a case-insensitive equality evaluation
/// can be achieved using custom hashing and comparison functions.<br>
/// At the point the function is called, both the hash and length equality have
/// been already checked. <br>
/// If this interface is not used, the comparison will fall back to a binary
/// equality check using `memcmp()`.
///
/// <hr>
///
/// Short code snippets can be found on page @ref examples
/// "Hash Map / Hash Set Example Code".
///
/// <hr>
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
/// @page examples   Hash Map / Hash Set Example Code
/// <hr>
/// @section hm_ex   Hash Map Example:
/// @code{.c}
/// #include <stdio.h>
/// #include "hm.h"
///
/// int main(void)
/// {
///   static const char key[] = "abc";
///   const size_t keyLen = sizeof(key) / sizeof(key[0]) - 1;
///
///   // create a hash map (with default hashing algorithm and default comparer)
///   hm_t hm = hm_create(NULL, 0, NULL);
///   if (hm == NULL)
///     return 1;
///
///   // add an element
///   hm_add(hm, key, keyLen, (int[]){ 5 }, sizeof(int)); // the array (compound literal) decays into a pointer to int 5
///
///   // get the data of the element with a certain key
///   hm_iter_t item = hm_item(hm, key, keyLen);
///   if (item != NULL)
///     printf("\"abc\" is associated with %d\n", *(int *)(item->val));
///   else
///     puts("\"abc\" not found");
///
///   // release allocated resources
///   hm_destroy(hm);
///
///   return 0;
/// }
/// @endcode
/// <br><hr>
/// @section hs_ex   Hash Set Example:
/// @code{.c}
/// #include <stdio.h>
/// #include "hm.h"
///
/// int main(void)
/// {
///   static const char val[] = "abc";
///   const size_t valLen = sizeof(val) / sizeof(val[0]) - 1;
///
///   // create a hash set (with default hashing algorithm and default comparer)
///   hs_t hs = hs_create(NULL, 0, NULL);
///   if (hs == NULL)
///     return 1;
///
///   // add an element
///   hs_add(hs, val, valLen);
///
///   // check whether the element is contained
///   if (hs_contains(hs, val, valLen))
///     puts("\"abc\" found");
///   else
///     puts("\"abc\" not found");
///
///   // release allocated resources
///   hs_destroy(hs);
///
///   return 0;
/// }
/// @endcode
/// <br><hr>
/// @section ignore_case_ex   Example of a function set for a case-insensitive string hashing/comparison:
/// @code{.c}
/// #include <ctype.h>
/// #include "hm.h"
///
/// uint64_t case_insensitive_hash(const void *key, size_t keyLen, uint64_t hashSeed)
/// {
///   // FNV-1a algorithm with each character converted using `toupper()`
///   (void)hashSeed;
///   uint64_t hash = UINT64_C(0xCBF29CE484222325);
///   for (const uint8_t *byteIt = (const uint8_t *)key, *const end = byteIt + keyLen; byteIt < end; ++byteIt)
///     hash = (hash ^ (uint8_t)toupper(*byteIt)) * UINT64_C(0x00000100000001B3);
///
///   return hash;
/// }
///
/// bool case_insensitive_commparer(const void *key1, const void *key2, size_t len)
/// {
///   // character-wise comparison with each character converted using `toupper()`
///   for (const uint8_t *key1It = (const uint8_t *)key1, *key2It = (const uint8_t *)key2, *const end = key1It + len; key1It < end; ++key1It, ++key2It)
///     if (toupper(*key1It) != toupper(*key2It))
///       return false;
///
///   return true;
/// }
///
/// // to be used like so:
/// // hm_t hm = hm_create(&case_insensitive_hash, 0, &case_insensitive_commparer);
/// @endcode
/// <br>
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

#endif // HASHMAP_12AA98F5_9135_48EA_9AD3_8619146FAEAE
