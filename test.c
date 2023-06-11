#if defined(_MSC_VER) && !defined(__GNUC__) && !defined(__clang__)
#  define _CRT_SECURE_NO_WARNINGS // sprintf()
#endif

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "hm.h"

// Uncomment the macro definition to use XXH3 as an example for a custom hash function.
// NOTE: Requires "xxhash.h" (header only) attached to your project, refer to: https://github.com/Cyan4973/xxHash
// #define USE_XXH3

#if defined(USE_XXH3)
// we do not own "xxhash.h", so warnings are just ignored
#  if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#    pragma clang diagnostic ignored "-Wunused-macros"
#    pragma clang diagnostic ignored "-Wused-but-marked-unused"
#  elif defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4711 4820 5045 6297 26451)
#  endif
#  define XXH_INLINE_ALL
#  include "xxhash.h"
#  if defined(__clang__)
#    pragma clang diagnostic pop
#  elif defined(_MSC_VER)
#    pragma warning(pop)
#  endif
#  include <time.h>

#  define HASH_FUNC &XXH_INLINE_XXH3_64bits_withSeed // use the XXH3 algorithm accessible via `XXH3_64bits_withSeed()` (the prefix "XXH_INLINE_" is a macro hack in "xxhash.h", not necessary but comforts the static analysis)

static uint64_t get_seed_(void) // scramble the bits returned by time() and clock()
{
  uint64_t val = ((uint64_t)time(NULL) ^ ((uint64_t)clock() << 43U)) * UINT64_C(0x591FCB9CE4E8D3D5);
  val = (val ^ ((val << 31U) | (val >> 33U)) ^ ((val << 13U) | (val >> 51U)) ^ ((val << 53U) | (val >> 11U)) ^ ((val << 19U) | (val >> 45U)) ^ ((val << 41U) | (val >> 23U))) * UINT64_C(0xC4E6FDE7561C7CB3);
  return val;
}

#else // !defined(USE_XXH3)

#  define HASH_FUNC NULL // use the default FNV-1a algorithm

static uint64_t get_seed_(void) // seed is ignored in the default hash algorithm, so we just return 0
{
  return UINT64_C(0);
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeclaration-after-statement" // C99 is required anyway, no issue here
#  if defined(__clang_major__) && (__clang_major__ >= 16)
#    pragma clang diagnostic ignored "-Wunsafe-buffer-usage" // yes, of course we perform pointer arithmetics in C
#  endif
#elif defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4710) // function not inline
#  pragma warning(disable : 4996) // sprintf() may be unsafe
#  pragma warning(disable : 5045) // spectre mitigation possibly inserted
#endif

static const char text[] = "Lorem ipsum dolor sit amet, consetetur sadipscing elitr,"
                           " sed diam nonumy eirmod tempor invidunt ut labore et dolore"
                           " magna aliquyam erat, sed diam voluptua. At vero eos et accusam"
                           " et justo duo dolores et ea rebum. Stet clita kasd gubergren,"
                           " no sea takimata sanctus est Lorem ipsum dolor sit amet.";

static void trivially_count_characters(void)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  const unsigned initialCnt = 1U;

  puts(text);
  puts("");

  hm_t hm = hm_create(HASH_FUNC, get_seed_(), NULL);
  if (!hm)
  {
    puts("!!!!! error !!!!!");
    return;
  }

  // iterate the text and create the hash map using the characters as key each
  for (const char *pCh = text; *pCh; ++pCh)
  {
    const hm_iter_t pItem = hm_item(hm, pCh, sizeof(char)); // try to get the item with the current character as key
    if (pItem) // the character exists, we use the unsafe interface to increment the counter because the value is an unsigned int and its size does never change
      ++*(unsigned *)pItem->val;
    else // the character does not exist, we need to add it
      hm_add(hm, pCh, sizeof(char), &initialCnt, sizeof(unsigned));
  }

  // iterate the hash map and print the found characters along with the number of their occurrences in the text
  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    printf("'%c'%11u\n", *(const char *)itemIt->key, *(unsigned *)itemIt->val);

  // hm_item() is likely the most commonly used function to query a hash map
  const hm_iter_t pItem = hm_item(hm, "a", sizeof(char));
  if (pItem)
    printf("\nCharacter 'a' occurs %u times in the text.\n\n", *(unsigned *)pItem->val);
  else
    puts("\nCharacter 'a' not found in the text.\n");

  hm_destroy(hm);
}

static void HmCapacity_TEST(hm_t *hmPtr)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  // begin with a capacity of 192, then add 32768 values
  *hmPtr = hm_create(HASH_FUNC, get_seed_(), NULL); // 192 is the lowest capacity of a hash map in this interface
  if (!*hmPtr)
  {
    puts("!!!!! error !!!!!");
    exit(1);
  }

  printf(" hm_create()\nInitial capacity (  192 expected):   %zu\n", hm_capacity(*hmPtr));

  char buffer[32];
  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hm_add(*hmPtr, buffer, 4, &i, sizeof(i));
  }

  const hm_iter_t front = hm_next(*hmPtr, NULL);
  hm_iter_t back = NULL;
  for (hm_iter_t itemIt = front; itemIt != NULL; itemIt = hm_next(*hmPtr, itemIt))
    back = itemIt;

  printf(
    "Number of values (32768 expected): %zu\nFirst value (0000 expected): %04X\nLast value  (7FFF expected): %04X\n\n",
    hm_length(*hmPtr),
    front ? *(const unsigned *)front->val : UINT32_MAX,
    back ? *(const unsigned *)back->val : UINT32_MAX);

  hm_destroy(*hmPtr);

  // start from scratch but begin with a higher initial capacity, prove that increasing still works
  *hmPtr = hm_create_capacity(HASH_FUNC, get_seed_(), NULL, 15000); // next higher is 24576
  if (!*hmPtr)
  {
    puts("!!!!! error !!!!!");
    exit(1);
  }

  printf(" hm_create_capacity()\nInitial capacity (24576 expected): %zu\n", hm_capacity(*hmPtr));

  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hm_add(*hmPtr, buffer, 4, &i, sizeof(i));
  }

  printf("Final capacity   (49152 expected): %zu\n\n", hm_capacity(*hmPtr));
}

static void HmUpdate_TEST(hm_t hm)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  // unsafe interface
  hm_iter_t pItem = hm_item(hm, "0123", 4);
  unsigned *const pDangerous = pItem->val;
  printf("Unsafe interface (0123 expected): %04X\n", *pDangerous);
  ++*pDangerous; // increment the value, this does not change the size of the value as the type is still an unsigned int
  pItem = hm_item(hm, "0123", 4); // this is redundant, it just makes it clearer that the item in the map has actually been updated
  printf("Incremented      (0124 expected): %04X\n\n", *(const unsigned *)pItem->val);

  static const char s[] = "foobarbaz";

  // yes we can replace the unsigned integer with a string, although better keep the data type consistent in the whole map ;-)
  hm_update(hm, "0123", 4, s, 3);
  pItem = hm_item(hm, "0123", 4);
  printf("Update (foo       expected): %s\n", (const char *)pItem->val);

  hm_update(hm, "0123", 4, s, 6);
  pItem = hm_item(hm, "0123", 4);
  printf("Update (foobar    expected): %s\n", (const char *)pItem->val);

  // and a NULL pointer is valid data
  hm_update(hm, "0123", 4, NULL, 0);
  pItem = hm_item(hm, "0123", 4);
  printf("Update (NULL      expected): %s\n", pItem->val == NULL ? "NULL" : (const char *)pItem->val);

  hm_remove(hm, "0123", 4);

  // hm_update works like hm_add if the key does not exist
  hm_update(hm, "0123", 4, s, 9);
  pItem = hm_item(hm, "0123", 4);
  printf("Update (foobarbaz expected): %s\n", (const char *)pItem->val);

  puts("");

  hm_t hmNew = hm_create_capacity(HASH_FUNC, get_seed_(), NULL, 500); // next higher is 768
  if (!hmNew)
  {
    puts("!!!!! error !!!!!");
    return;
  }

  char buffer[32];
  for (unsigned i = 32668; i < 32780; ++i) //  first 100 values overlap with keys in hm
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hm_add(hmNew, buffer, 4, &i, sizeof(i));
  }

  printf("Capacity src  (  768 expected): %5zu\n", hm_capacity(hmNew));
  printf("Length   src  (  112 expected): %5zu\n", hm_length(hmNew));
  printf("Capacity dest (49152 expected): %5zu\n", hm_capacity(hm));
  printf("Length   dest (32768 expected): %5zu\n\n", hm_length(hm));

  hm_merge(hm, hmNew, false);

  printf("Capacity src  (  768 expected): %5zu\n", hm_capacity(hmNew));
  printf("Length   src  (  100 expected): %5zu\n", hm_length(hmNew));
  printf("Capacity dest (49152 expected): %5zu\n", hm_capacity(hm));
  printf("Length   dest (32780 expected): %5zu\n\n", hm_length(hm));

  hm_shrink(hmNew);

  printf("Capacity src  (  192 expected): %5zu\n", hm_capacity(hmNew));
  printf("Length   src  (  100 expected): %5zu\n", hm_length(hmNew));
  printf("first remaining ( 7F9C expected):  %s\n", (const char *)(hm_item(hmNew, "7F9C", 4)->key));
  printf("last remaining  ( 7FFF expected):  %s\n\n", (const char *)(hm_item(hmNew, "7FFF", 4)->key));

  hm_merge(hm, hmNew, true);

  printf("Capacity src  (  192 expected): %5zu\n", hm_capacity(hmNew));
  printf("Length   src  (    0 expected): %5zu\n", hm_length(hmNew));

  hm_destroy(hmNew);
}

static void HmRemove_TEST(hm_t hm)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  bool ret = hm_remove(hm, "0fff", 4);
  printf("Remove   0fff (false expected): %s\n", ret ? "true" : "false");

  ret = hm_remove(hm, "0FFF", 4);
  printf("Remove   0FFF (true  expected): %s\n\n", ret ? "true" : "false");

  ret = hm_contains(hm, "0FFF", 4);
  printf("Contains 0FFF (false expected): %s\n\n", ret ? "true" : "false");

  size_t valLen = 0;
  const unsigned *const pDeached = hm_detach(hm, "1000", 4, &valLen);
  printf("Detach 1000 (1000 expected): %04X\nLength      (   4 expected):    %zu\n\n", *pDeached, valLen);
  hm_free_detached(pDeached);

  const hm_iter_t back = hm_prev(hm, NULL);
  hm_iter_t front = NULL;
  for (hm_iter_t itemIt = back; itemIt != NULL; itemIt = hm_prev(hm, itemIt))
    front = itemIt;

  printf(
    "Number of values (32778 expected): %zu\nLast value  (800B expected): %04X\nFirst value (0000 expected): %04X\n\n",
    hm_length(hm),
    back ? *(const unsigned *)back->val : UINT32_MAX,
    front ? *(const unsigned *)front->val : UINT32_MAX);
}

static void HmClear_TEST(hm_t hm)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  printf("Capacity (49152 expected): %zu\n", hm_capacity(hm));
  printf("Length   (32778 expected): %zu\n", hm_length(hm));
  printf("Empty    (false expected): %s\n\n", hm_empty(hm) ? "true" : "false");

  hm_clear(hm);

  printf("Capacity (  192 expected):   %zu\n", hm_capacity(hm));
  printf("Length   (    0 expected):     %zu\n", hm_length(hm));
  printf("Empty    (true  expected): %s\n\n", hm_empty(hm) ? "true" : "false");
}

static void roundtrip_fill_(hm_t hm)
{
  printf(" %s\n", __func__);
  char buffer[32];
  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    if (!hm_add(hm, buffer, 4, &i, sizeof(i)))
      puts("error 2");
  }

  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    if (!hm_contains(hm, itemIt->key, itemIt->keyLen))
      puts("error 3");

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\n\n", cap, len);
  if (cap != 49152U || len != 32768U)
    puts("error 4");
}

static void roundtrip_remove_(hm_t hm)
{
  printf(" %s\n", __func__);
  char buffer[32];
  for (unsigned i = 0; i < 32768; i += 2)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    if (!hm_remove(hm, buffer, 4))
      puts("error 5");
  }

  unsigned cnt = 0U;
  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
  {
    ++cnt;
    if (!hm_contains(hm, itemIt->key, itemIt->keyLen))
      puts("error 6");
  }

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\ncounted  %5u\n\n", cap, len, cnt);
  if (cap != 49152U || len != 16384U || cnt != 16384U)
    puts("error 7");
}

static void roundtrip_shrink_(hm_t hm)
{
  printf(" %s\n", __func__);
  if (!hm_shrink(hm))
    puts("error 8");

  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    if (!hm_contains(hm, itemIt->key, itemIt->keyLen))
      puts("error 9");

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\n\n", cap, len);
  if (cap != 24576U || len != 16384U)
    puts("error 10");
}

static void roundtrip_fill_new_(hm_t hmNew)
{
  printf(" %s\n", __func__);
  char buffer[32];
  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    if (!hm_add(hmNew, buffer, 4, &i, sizeof(i)))
      puts("error 12");
  }

  for (hm_iter_t itemIt = hm_next(hmNew, NULL); itemIt; itemIt = hm_next(hmNew, itemIt))
    if (!hm_contains(hmNew, itemIt->key, itemIt->keyLen))
      puts("error 13");

  const size_t capNew = hm_capacity(hmNew);
  const size_t lenNew = hm_length(hmNew);
  printf("capacity new %5zu\nlength new   %5zu\n\n", capNew, lenNew);
  if (capNew != 49152U || lenNew != 32768U)
    puts("error 14");
}

static void roundtrip_merge_add_(hm_t hm, hm_t hmNew)
{
  printf(" %s\n", __func__);
  if (!hm_merge(hm, hmNew, false))
    puts("error 15");

  for (hm_iter_t itemIt = hm_next(hmNew, NULL); itemIt; itemIt = hm_next(hmNew, itemIt))
    if (!hm_contains(hmNew, itemIt->key, itemIt->keyLen))
      puts("error 16");

  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    if (!hm_contains(hm, itemIt->key, itemIt->keyLen))
      puts("error 17");

  const size_t capNew = hm_capacity(hmNew);
  const size_t lenNew = hm_length(hmNew);
  printf("capacity new %5zu\nlength new   %5zu\n", capNew, lenNew);
  if (capNew != 49152U || lenNew != 16384U)
    puts("error 18");

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\n\n", cap, len);
  if (cap != 49152U || len != 32768U)
    puts("error 19");
}

static void roundtrip_merge_update_(hm_t hm, hm_t hmNew)
{
  printf(" %s\n", __func__);
  if (!hm_merge(hm, hmNew, true))
    puts("error 20");

  for (hm_iter_t itemIt = hm_next(hmNew, NULL); itemIt; itemIt = hm_next(hmNew, itemIt))
    if (!hm_contains(hmNew, itemIt->key, itemIt->keyLen))
      puts("error 21");

  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    if (!hm_contains(hm, itemIt->key, itemIt->keyLen))
      puts("error 22");

  const size_t capNew = hm_capacity(hmNew);
  const size_t lenNew = hm_length(hmNew);
  printf("capacity new %5zu\nlength new   %5zu\n", capNew, lenNew);
  if (capNew != 192U || lenNew != 0U)
    puts("error 23");

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\n\n", cap, len);
  if (cap != 49152U || len != 32768U)
    puts("error 24");
}

static void roundtrip_validate_number_(hmc_t hm)
{
  printf(" %s\n", __func__);
  unsigned cnt = 0U;
  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt; itemIt = hm_next(hm, itemIt))
    ++cnt;

  printf("counted  %5u\n\n", cnt);
  if (cnt != 32768U)
    puts("error 25");
}

static void roundtrip_detach_all_(hm_t hm)
{
  printf(" %s\n", __func__);
  unsigned cnt = 0U;
  size_t prevCap = hm_capacity(hm);
  for (hm_iter_t itemIt = hm_next(hm, NULL); itemIt;)
  {
    ++cnt;
    const void *val = hm_detach(hm, itemIt->key, itemIt->keyLen, NULL);
    if (val == NULL)
      puts("error 26");
    else
      hm_free_detached(val);

    // this tests a certain behavior of the private interface; in real code, never rely on the iterator being still valid after hm_detach()!
    if (hm_capacity(hm) != prevCap)
    {
      itemIt = hm_next(hm, NULL);
      prevCap = hm_capacity(hm);
    }
    else
      itemIt = hm_next(hm, itemIt);
  }

  const size_t cap = hm_capacity(hm);
  const size_t len = hm_length(hm);
  printf("capacity %5zu\nlength   %5zu\nremoved  %5u\n\n", cap, len, cnt);
  if (cap != 192U || len != 0U || cnt != 32768U)
    puts("error 27");
}

static void roundtrip_TEST(void)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  hm_t hm = hm_create(HASH_FUNC, get_seed_(), NULL);
  if (!hm)
  {
    puts("error 1");
    return;
  }

  roundtrip_fill_(hm);
  roundtrip_remove_(hm);
  roundtrip_shrink_(hm);

  hm_t hmNew = hm_create(HASH_FUNC, get_seed_(), NULL);
  if (!hmNew)
  {
    puts("error 11");
    hm_destroy(hm);
    return;
  }

  roundtrip_fill_new_(hmNew);
  roundtrip_merge_add_(hm, hmNew);
  roundtrip_merge_update_(hm, hmNew);
  roundtrip_validate_number_(hm);
  roundtrip_detach_all_(hm);

  hm_destroy(hmNew);
  hm_destroy(hm);
}

typedef struct
{
  uint8_t b; // 1 byte
  // here are 3 bytes padding with undefined content which is the reason why memcmp may fail to determine equality
  uint32_t i; // 4 bytes
} comp_test_key_t;

static uint64_t comp_test_hasher_(const void *data, size_t dataLen, uint64_t hashSeed)
{
  (void)dataLen;
  (void)hashSeed;
  const comp_test_key_t *key = (const comp_test_key_t *)data;

  uint64_t hash = UINT64_C(0xCBF29CE484222325);
  hash = (hash ^ key->b) * UINT64_C(0x00000100000001B3); // b member
  for (const uint8_t *byteIt = (const uint8_t *)(&(key->i)), *const end = byteIt + 4; byteIt < end; ++byteIt) // i member
    hash = (hash ^ *byteIt) * UINT64_C(0x00000100000001B3);

  return hash;
}

static bool comp_test_commparer_(const void *x, const void *y, size_t len)
{
  (void)len;
  const comp_test_key_t *const key1 = (const comp_test_key_t *)x;
  const comp_test_key_t *const key2 = (const comp_test_key_t *)y;
  return (key1->b == key2->b) && (key1->i == key2->i);
}

static void comparer_TEST(void)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  hm_t hm = hm_create(&comp_test_hasher_, 0, &comp_test_commparer_);
  if (!hm)
  {
    puts("error");
    return;
  }

  puts(hm_add(hm, (comp_test_key_t[]){ { 1, 2 } }, sizeof(comp_test_key_t), "x", 1) == 1 ? "OK" : "NOK");
  puts(hm_add(hm, (comp_test_key_t[]){ { 2, 3 } }, sizeof(comp_test_key_t), "y", 1) == 1 ? "OK" : "NOK");
  puts(hm_add(hm, (comp_test_key_t[]){ { 1, 2 } }, sizeof(comp_test_key_t), "z", 1) == 1 ? "NOK" : "OK"); // same key like the first
  puts("");
  puts(hm_contains(hm, (comp_test_key_t[]){ { 1, 2 } }, sizeof(comp_test_key_t)) ? "OK" : "NOK");
  puts(hm_contains(hm, (comp_test_key_t[]){ { 2, 3 } }, sizeof(comp_test_key_t)) ? "OK" : "NOK");
  puts(hm_contains(hm, (comp_test_key_t[]){ { 4, 5 } }, sizeof(comp_test_key_t)) ? "NOK" : "OK"); // unknown key
  puts("");

  hm_destroy(hm);
}

static uint64_t case_insensitive_test_hasher_(const void *data, size_t dataLen, uint64_t hashSeed)
{
  (void)hashSeed;
  uint64_t hash = UINT64_C(0xCBF29CE484222325);
  for (const uint8_t *byteIt = (const uint8_t *)data, *const end = byteIt + dataLen; byteIt < end; ++byteIt)
    hash = (hash ^ (uint8_t)toupper(*byteIt)) * UINT64_C(0x00000100000001B3);

  return hash;
}

static bool case_insensitive_test_commparer_(const void *x, const void *y, size_t len)
{
  for (const uint8_t *key1It = (const uint8_t *)x, *key2It = (const uint8_t *)y, *const end = key1It + len; key1It < end; ++key1It, ++key2It)
    if (toupper(*key1It) != toupper(*key2It))
      return false;

  return true;
}

static void case_insensitive_TEST(void)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  hm_t hm = hm_create(&case_insensitive_test_hasher_, 0, &case_insensitive_test_commparer_);
  if (!hm)
  {
    puts("error");
    return;
  }

  char buffer[32];
  for (unsigned i = 10; i < 16; ++i)
  {
    sprintf(buffer, "%04X", i); // NOLINT
    printf("%s %s\n", buffer, hm_add(hm, buffer, 4, &i, sizeof(i)) == 1 ? "added     - OK" : "not added - NOK");
    sprintf(buffer, "%04x", i); // NOLINT
    printf("%s %s\n", buffer, hm_add(hm, buffer, 4, &i, sizeof(i)) == 1 ? "added     - NOK" : "not added - OK"); // lowercase shall be treated the same and thus, not added
  }

  puts("");
  printf("%s %s\n", "000A", hm_contains(hm, "000A", 4) ? "exists         - OK" : "does not exist - NOK"); // actually added key value
  printf("%s %s\n", "000a", hm_contains(hm, "000a", 4) ? "exists         - OK" : "does not exist - NOK"); // lower case shall be treated equal
  printf("%s %s\n", "000x", hm_contains(hm, "000x", 4) ? "exists         - NOK" : "does not exist - OK"); // unknown key
  puts("");

  hm_destroy(hm);
}

static void trivially_unique_characters(void)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  puts(text);
  puts("");

  hs_t hs = hs_create(HASH_FUNC, get_seed_(), NULL);
  if (!hs)
  {
    puts("!!!!! error !!!!!");
    return;
  }

  // iterate the text and create the hash set using the characters as value each
  for (const char *pCh = text; *pCh; ++pCh)
    hs_add(hs, pCh, sizeof(char));

  // iterate the hash set and print the row of unique characters found in the text
  for (hs_iter_t itemIt = hs_next(hs, NULL); itemIt; itemIt = hs_next(hs, itemIt))
    printf("%c", *(const char *)itemIt->val);

  puts("\n");

  // hs_contains() is likely the most commonly used function to query a hash set
  if (hs_contains(hs, "a", sizeof(char)))
    puts("Character 'a' exists in the text.\n");
  else
    puts("Character 'a' not found in the text.\n");

  hs_destroy(hs);
}

static void HsCapacity_TEST(hs_t *hsPtr)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  // begin with a capacity of 192, then add 32768 values
  *hsPtr = hs_create(HASH_FUNC, get_seed_(), NULL); // 192 is the lowest capacity of a hash map in this interface
  if (!*hsPtr)
  {
    puts("!!!!! error !!!!!");
    exit(1);
  }

  printf(" hs_create()\nInitial capacity (  192 expected):   %zu\n", hs_capacity(*hsPtr));

  char buffer[32];
  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hs_add(*hsPtr, buffer, 4);
  }

  const hs_iter_t front = hs_next(*hsPtr, NULL);
  hs_iter_t back = NULL;
  for (hs_iter_t itemIt = front; itemIt != NULL; itemIt = hs_next(*hsPtr, itemIt))
    back = itemIt;

  printf(
    "Number of values (32768 expected): %zu\nFirst value (0000 expected): %s\nLast value  (7FFF expected): %s\n\n",
    hs_length(*hsPtr),
    front ? (const char *)front->val : "NULL",
    back ? (const char *)back->val : "NULL");

  hs_destroy(*hsPtr);

  // start from scratch but begin with a higher initial capacity, prove that increasing still works
  *hsPtr = hs_create_capacity(HASH_FUNC, get_seed_(), NULL, 15000); // next higher is 24576
  if (!*hsPtr)
  {
    puts("!!!!! error !!!!!");
    exit(1);
  }

  printf(" hs_create_capacity()\nInitial capacity (24576 expected): %zu\n", hs_capacity(*hsPtr));

  for (unsigned i = 0; i < 32768; ++i)
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hs_add(*hsPtr, buffer, 4);
  }

  printf("Final capacity   (49152 expected): %zu\n\n", hs_capacity(*hsPtr));
}

static void HsRemove_TEST(hs_t hs)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  bool ret = hs_remove(hs, "0fff", 4);
  printf("Remove   0fff (false expected): %s\n", ret ? "true" : "false");

  ret = hs_remove(hs, "0FFF", 4);
  printf("Remove   0FFF (true  expected): %s\n\n", ret ? "true" : "false");

  ret = hs_contains(hs, "0FFF", 4);
  printf("Contains 0FFF (false expected): %s\n\n", ret ? "true" : "false");

  const hs_iter_t back = hs_prev(hs, NULL);
  hs_iter_t front = NULL;
  for (hs_iter_t itemIt = back; itemIt != NULL; itemIt = hs_prev(hs, itemIt))
    front = itemIt;

  printf(
    "Number of values (32767 expected): %zu\nLast value  (7FFF expected): %s\nFirst value (0000 expected): %s\n\n",
    hs_length(hs),
    back ? (const char *)back->val : "NULL",
    front ? (const char *)front->val : "NULL");

  hs_t hsNew = hs_create_capacity(HASH_FUNC, get_seed_(), NULL, 500);
  if (!hsNew)
  {
    puts("!!!!! error !!!!!");
    return;
  }

  char buffer[32];
  for (unsigned i = 32668; i < 32780; ++i) // first 100 values overlap with existing values in hs
  {
    // NOLINTNEXTLINE
    sprintf(buffer, "%04X", i); // clang-tidy prefers sprintf_s, however there is no doubt that the buffer is large enough
    hs_add(hsNew, buffer, 4);
  }

  printf("Capacity src  (  768 expected): %5zu\n", hs_capacity(hsNew));
  printf("Length   src  (  112 expected): %5zu\n", hs_length(hsNew));
  printf("Capacity dest (49152 expected): %5zu\n", hs_capacity(hs));
  printf("Length   dest (32767 expected): %5zu\n\n", hs_length(hs));

  hs_merge(hs, hsNew);

  printf("Capacity src  (  768 expected): %5zu\n", hs_capacity(hsNew));
  printf("Length   src  (  100 expected): %5zu\n", hs_length(hsNew));
  printf("Capacity dest (49152 expected): %5zu\n", hs_capacity(hs));
  printf("Length   dest (32779 expected): %5zu\n\n", hs_length(hs));

  hs_shrink(hsNew);

  printf("Capacity src  (  192 expected): %5zu\n", hs_capacity(hsNew));
  printf("Length   src  (  100 expected): %5zu\n", hs_length(hsNew));
  printf("first remaining ( 7F9C expected):  %s\n", (const char *)(hs_item(hsNew, "7F9C", 4)->val));
  printf("last remaining  ( 7FFF expected):  %s\n\n", (const char *)(hs_item(hsNew, "7FFF", 4)->val));

  hs_destroy(hsNew);
}

static void HsClear_TEST(hs_t hs)
{
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n*** %s ***\n\n", __func__);

  hs_iter_t item = hs_item(hs, "0000", 4);
  printf("item 0000 (0000 expected): %s\n", item == NULL ? "NULL" : (const char *)item->val);
  printf("Capacity (49152 expected): %zu\n", hs_capacity(hs));
  printf("Length   (32779 expected): %zu\n", hs_length(hs));
  printf("Empty    (false expected): %s\n\n", hs_empty(hs) ? "true" : "false");

  hs_clear(hs);

  item = hs_item(hs, "0000", 4);
  printf("item 0000 (NULL expected): %s\n", item == NULL ? "NULL" : (const char *)item->val);
  printf("Capacity (  192 expected):   %zu\n", hs_capacity(hs));
  printf("Length   (    0 expected):     %zu\n", hs_length(hs));
  printf("Empty    (true  expected): %s\n\n", hs_empty(hs) ? "true" : "false");
}

int main(void)
{
  puts("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n  ~~~ Hash Map Interface ~~~");

  trivially_count_characters(); // this is a very simple application to demonstrate the functionality of a hash map

  /*
  Index of all hash map interface functions:
  hm_create()           [^1]
  hm_create_capacity()  [^2]
  hm_add()              [^3]
  hm_update()           [^4]
  hm_merge()            [^5]
  hm_detach()           [^6]
  hm_remove()           [^7]
  hm_contains()         [^8]
  hm_item()             [^9]
  hm_next()            [^10]
  hm_prev()            [^11]
  hm_empty()           [^12]
  hm_length()          [^13]
  hm_capacity()        [^14]
  hm_free_detached()   [^15]
  hm_shrink()          [^16]
  hm_clear()           [^17]
  hm_destroy()         [^18]
  */

  hm_t hm = NULL;
  HmCapacity_TEST(&hm); // [^1] [^2] [^3] ---- ---- ---- ---- ---- ---- [^10] ----- ----- [^13] [^14] ----- ----- ----- [^18]
  HmUpdate_TEST(hm); //    ---- [^2] [^3] [^4] [^5] ---- [^7] ---- [^9] ----- ----- ----- [^13] [^14] ----- [^16] ----- [^18]
  HmRemove_TEST(hm); //    ---- ---- ---- ---- ---- [^6] [^7] [^8] ---- ----- [^11] ----- [^13] ----- [^15] ----- ----- -----
  HmClear_TEST(hm); //     ---- ---- ---- ---- ---- ---- ---- ---- ---- ----- ----- [^12] [^13] [^14] ----- ----- [^17] -----
  hm_destroy(hm);

  roundtrip_TEST(); // this is an expensive test that verifies all return values and compares querying using iterators with querying using key access several times

  comparer_TEST();

  case_insensitive_TEST();

  puts("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n  ~~~ Hash Set Interface ~~~");

  trivially_unique_characters(); // this is a very simple application to demonstrate the functionality of a hash set

  /*
  Index of all hash set interface functions:
  hs_create()           [^1]
  hs_create_capacity()  [^2]
  hs_add()              [^3]
  hs_merge()            [^4]
  hs_remove()           [^5]
  hs_contains()         [^6]
  hs_item()             [^7]
  hs_next()             [^8]
  hs_prev()             [^9]
  hs_empty()           [^10]
  hs_length()          [^11]
  hs_capacity()        [^12]
  hs_shrink()          [^13]
  hs_clear()           [^14]
  hs_destroy()         [^15]
  */

  hs_t hs = NULL;
  HsCapacity_TEST(&hs); // [^1] [^2] [^3] ---- ---- ---- ---- [^8] ---- ----- [^11] [^12] ----- ----- [^15]
  HsRemove_TEST(hs); //    ---- [^2] [^3] [^4] [^5] [^6] [^7] ---- [^9] ----- [^11] [^12] [^13] ----- [^15]
  HsClear_TEST(hs); //     ---- ---- ---- ---- ---- ---- [^7] ---- ---- [^10] [^11] [^12] ----- [^14] -----
  hs_destroy(hs);
  return 0;
}

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif
