/**
 * mymalloc.c - A basic first-fit memory allocator built on sbrk.
 *
 * Design:
 *   Every block obtained from sbrk is preceded by a block_t header. All
 *   headers (free and in use) form a single singly-linked list, kept in the
 *   order the blocks were obtained from sbrk; new blocks are appended at the
 *   tail. Allocation walks the list from the head and reuses the first free
 *   block whose size is large enough (first fit). If no such block exists,
 *   exactly sizeof(block_t) + size bytes are requested with sbrk.
 *
 *   Heap layout:
 *   |block_t|---user memory---|block_t|------user memory------| ...
 *           ^ pointer returned to the caller
 *
 * Assumptions:
 *   - Single-threaded use (no locking).
 *   - Nobody else moves the program break between our sbrk calls in a way
 *     that matters to us: we only follow the pointers sbrk hands back.
 *   - No alignment beyond what sbrk provides is required.
 *   - Blocks are never split, coalesced, or returned to the OS.
 */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

// NOTE: You should NOT include <stdlib.h> in your final implementation.
// malloc.h turns every call to malloc/calloc/free in this file into a call
// to mymalloc/mycalloc/myfree, so calling them here would recurse forever.

#include <debug.h> // definition of debug_printf

/** Header placed immediately before each chunk of user memory. */
typedef struct block {
  size_t size;        // usable bytes following this header
  struct block *next; // next block in sbrk order (free or in use), or NULL
  int free;           // 1 if the block is available, 0 if in use
} block_t;

#define BLOCK_SIZE sizeof(block_t)

/** The list of all blocks; head is the oldest, tail the most recent. */
static struct {
  block_t *head;
  block_t *tail;
} heap_list = { NULL, NULL };

/**
 * Find the first free block that can hold the requested number of bytes.
 *
 * @param size number of bytes needed (> 0)
 * @return the first suitable free block in list order, or NULL if none
 */
static block_t *find_free_block(size_t size) {
  for (block_t *cur = heap_list.head; cur != NULL; cur = cur->next) {
    if (cur->free && cur->size >= size) {
      return cur;
    }
  }
  return NULL;
}

/**
 * Request a new block from the OS with sbrk and append it to the list.
 * Exactly BLOCK_SIZE + size bytes are requested.
 *
 * @param size number of usable bytes needed (> 0)
 * @return the new (in use) block, or NULL if sbrk fails or size is too big
 */
static block_t *request_block(size_t size) {
  if (size > (size_t) INTPTR_MAX - BLOCK_SIZE) {
    return NULL; // the increment would not fit in sbrk's intptr_t argument
  }

  void *mem = sbrk((intptr_t) (BLOCK_SIZE + size));
  if (mem == (void *) -1) {
    return NULL;
  }

  block_t *block = (block_t *) mem;
  block->size = size;
  block->next = NULL;
  block->free = 0;

  if (heap_list.tail == NULL) {
    assert(heap_list.head == NULL);
    heap_list.head = block;
  } else {
    heap_list.tail->next = block;
  }
  heap_list.tail = block;
  return block;
}

/**
 * Get the header of a block from the pointer that was given to the user.
 *
 * @param ptr non-NULL pointer previously returned by mymalloc/mycalloc
 * @return the block_t header that precedes ptr
 */
static block_t *block_from_ptr(void *ptr) {
  assert(ptr != NULL);
  return (block_t *) ptr - 1;
}

/**
 * Allocate size bytes of uninitialized memory using a first-fit policy.
 *
 * @param s number of bytes requested
 * @return pointer to the usable memory, or NULL if s == 0 or out of memory
 */
void *mymalloc(size_t s) {
  if (s == 0) {
    return NULL;
  }

  block_t *block = find_free_block(s);
  if (block != NULL) {
    assert(block->free && block->size >= s);
    block->free = 0;
  } else {
    block = request_block(s);
    if (block == NULL) {
      return NULL; // out of memory (sbrk failed)
    }
  }
  debug_printf("Malloc %zu bytes\n", s);

  // The user's memory starts right after the header
  return (void *) (block + 1);
}

/**
 * Allocate zero-initialized memory for an array of nmemb elements of s bytes.
 *
 * @param nmemb number of elements
 * @param s size of each element in bytes
 * @return pointer to zeroed memory, or NULL if the total is 0, the
 *         multiplication overflows, or memory cannot be obtained
 */
void *mycalloc(size_t nmemb, size_t s) {
  if (s != 0 && nmemb > SIZE_MAX / s) {
    return NULL; // nmemb * s overflows
  }
  size_t total = nmemb * s;

  void *p = mymalloc(total);
  if (p == NULL) {
    return NULL; // zero-size request or out of memory
  }
  // A reused block may contain old data, so always clear it
  memset(p, 0, total);
  debug_printf("Calloc %zu bytes\n", total);

  return p;
}

/**
 * Mark the block owning ptr as free so it can be reused. Memory is never
 * returned to the OS.
 *
 * @param ptr pointer previously returned by mymalloc/mycalloc, or NULL
 */
void myfree(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  block_t *block = block_from_ptr(ptr);
  assert(!block->free); // catches (some) double frees
  block->free = 1;

  debug_printf("Freed %zu bytes\n", block->size);
}
