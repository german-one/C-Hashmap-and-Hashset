# C interface of a hash map, incl. an implementation of a hash set.

Copyright (c) 2023 Steffen Illhardt  
Licensed under the MIT license ( https://opensource.org/license/mit/ )

Version 1.0  

<hr>


The file `hm.h` contains the `C` interface of both a Hash Map and a Hash Set
library, implemented in the `hm.c` source file. <br>
The comments in the header are [Doxygen](https://www.doxygen.nl/index.html)
compliant for the generation of a documentation of the entire library.

Adding to the container copies both keys and values and automatically
appends null bytes, sufficient to serve as terminators for any string type,
if necessary. Of course, the interfaces can also be used for binary
data. <br>
The length of keys and values is defined at the element level to allow for
dynamically sized strings, arrays, or serialized data. <br>

<hr>

- A Hash Map (a.k.a. Hash Table) is an unordered container to store items as
key-value pairs where the keys are unique. <br>
This implementation allows NULL pointer values. <br>
Values can be detached from the map to possibly avoid copying them once
again. <br><br>

- A Hash Set is an unordered container to store unique values. <br>
Since a Hash Set can be treated as a Hash Map where the key is also the
value and with the value field of the map set to null, this implementation
of a Hash Set just wraps the Hash Map interface. <br><br>

- The Hashing Function Interface defines the pointer type
of a custom hashing function. It's shared with both the Hash Map and the
Hash Set interfaces. <br>
The aim of the function is to calculate hash values for keys of the Hash Map
or for values of the Hash Set, respectively. If they contain paddings with
undefined content (e.g. members of structures might be padded) you should
use a suitable algorithm for the calculation. Also, a case-insensitive
equality evaluation can be achieved using custom hashing and comparison
functions.<br>
Furthermore, there are many personal preferences for hashing algorithms.
Most of those should be able to get wrapped in a function with the
specification defined. <br>
If this interface is not used, the calculation of hash values will fall back
to a simple `FNV-1a` algorithm. <br><br>

- The Comparison Function Interface defines the pointer
type of a custom equality comparison function. It's shared with both the
Hash Map and the Hash Set interfaces. <br>
The aim of the function is to compare keys of the Hash Map or values of the
Hash Set, respectively. If they contain paddings with undefined content
(e.g. members of structures might be padded) you should use a suitable
algorithm for the comparison. Also, a case-insensitive equality evaluation
can be achieved using custom hashing and comparison functions.<br>
At the point the function is called, both the hash and length equality have
been already checked. <br>
If this interface is not used, the comparison will fall back to a binary
equality check using `memcmp()`.

<hr>

### Hash Map Example:

```c
#include <stdio.h>
#include "hm.h"

int main(void)
{
  static const char key[] = "abc";
  const size_t keyLen = sizeof(key) / sizeof(key[0]) - 1;

  // create a hash map (with default hashing algorithm and default comparer)
  hm_t hm = hm_create(NULL, 0, NULL);
  if (hm == NULL)
    return 1;

  // add an element
  hm_add(hm, key, keyLen, (int[]){ 5 }, sizeof(int)); // the array (compound literal) decays into a pointer to int 5

  // get the data of the element with a certain key
  hm_iter_t item = hm_item(hm, key, keyLen);
  if (item != NULL)
    printf("\"abc\" is associated with %d\n", *(int *)(item->val));
  else
    puts("\"abc\" not found");

  // release allocated resources
  hm_destroy(hm);

  return 0;
}
```

<br><hr>
### Hash Set Example:

```c
#include <stdio.h>
#include "hm.h"

int main(void)
{
  static const char val[] = "abc";
  const size_t valLen = sizeof(val) / sizeof(val[0]) - 1;

  // create a hash set (with default hashing algorithm and default comparer)
  hs_t hs = hs_create(NULL, 0, NULL);
  if (hs == NULL)
    return 1;

  // add an element
  hs_add(hs, val, valLen);

  // check whether the element is contained
  if (hs_contains(hs, val, valLen))
    puts("\"abc\" found");
  else
    puts("\"abc\" not found");

  // release allocated resources
  hs_destroy(hs);

  return 0;
}
```

<br><hr>
### Example of a function set for a case-insensitive string hashing/comparison:
```c
#include <ctype.h>
#include "hm.h"

uint64_t case_insensitive_hash(const void *key, size_t keyLen, uint64_t hashSeed)
{
  // FNV-1a algorithm with each character converted using `toupper()`
  (void)hashSeed;
  uint64_t hash = UINT64_C(0xCBF29CE484222325);
  for (const uint8_t *byteIt = (const uint8_t *)key, *const end = byteIt + keyLen; byteIt < end; ++byteIt)
    hash = (hash ^ (uint8_t)toupper(*byteIt)) * UINT64_C(0x00000100000001B3);

  return hash;
}

bool case_insensitive_commparer(const void *key1, const void *key2, size_t len)
{
  // character-wise comparison with each character converted using `toupper()`
  for (const uint8_t *key1It = (const uint8_t *)key1, *key2It = (const uint8_t *)key2, *const end = key1It + len; key1It < end; ++key1It, ++key2It)
    if (toupper(*key1It) != toupper(*key2It))
      return false;

  return true;
}

// to be used like so:
// hm_t hm = hm_create(&case_insensitive_hash, 0, &case_insensitive_commparer);
```
<br>
