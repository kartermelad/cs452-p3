#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "lab.h"

#define handle_error_and_die(msg) \
    do                            \
    {                             \
        perror(msg);              \
        raise(SIGKILL);          \
    } while (0)

/**
 * @brief Convert bytes to the correct K value
 *
 * @param bytes the number of bytes
 * @return size_t the K value that will fit bytes
 */
size_t btok(size_t bytes)
{
    if (!bytes) {
        return 0;
    }

    size_t k_val = 0;
    size_t mult = UINT64_C(1) << k_val;
    while (bytes > mult) {
        k_val++;     
        mult = UINT64_C(1) << k_val;
    }
    return k_val;
}

struct avail *buddy_calc(struct buddy_pool *pool, struct avail *buddy)
{
    if (!pool || !buddy) {
        return NULL;
    }
    size_t address = (size_t)buddy - (size_t)pool->base;
    size_t operand = UINT64_C(1) << buddy->kval;
    return (struct avail *)((address ^ operand) + (size_t)pool->base);
}

void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    if (!pool || size == 0) {
        errno = EINVAL;
        return NULL;
    }

    // Determine the smallest block size (k value) that can fit the requested size.
    size_t needed_k = btok(size + sizeof(struct avail));
    if (needed_k < SMALLEST_K) {
        needed_k = SMALLEST_K;
    }
    if (needed_k > pool->kval_m) {
        errno = ENOMEM;
        return NULL;
    }

    struct avail *block = NULL;
    size_t k;

    // Find the first available block of the required size or larger.
    for (k = needed_k; k <= pool->kval_m; k++) {
        struct avail *sentinel = &pool->avail[k];
        if (sentinel->next != sentinel) {
            block = sentinel->next;
            block->prev->next = block->next;
            block->next->prev = block->prev;
            break;
        }
    }
    if (!block) {
        errno = ENOMEM;
        return NULL;
    }

    // Split the block into smaller blocks until it matches the required size.
    while (block->kval > needed_k) {
        block->kval--;
        size_t new_k = block->kval;

        // Calculate the buddy block's address.
        struct avail *buddy = (struct avail *)((char *)block + ((size_t)1 << new_k));
        buddy->tag = BLOCK_AVAIL; // Mark the buddy as available.
        buddy->kval = new_k;

        // Add the buddy block to the free list for its size.
        struct avail *sentinel = &pool->avail[new_k];
        buddy->next = sentinel->next;
        buddy->prev = sentinel;
        sentinel->next->prev = buddy;
        sentinel->next = buddy;
    }
    block->tag = BLOCK_RESERVED;

    return (void *)((char *)block + sizeof(struct avail));
}

void buddy_free(struct buddy_pool *pool, void *ptr)
{
    if (!pool || !ptr) {
        return;
    }

    // Recover the block header from the user pointer
    struct avail *block = (struct avail *)((char *)ptr - sizeof(struct avail));
    block->tag = BLOCK_AVAIL;

    // Try to coalesce with buddy blocks
    while (block->kval < pool->kval_m) {
        struct avail *buddy = buddy_calc(pool, block);

        if (buddy->tag != BLOCK_AVAIL || buddy->kval != block->kval) {
            break;
        }

        // Remove the buddy from the free list
        buddy->prev->next = buddy->next;
        buddy->next->prev = buddy->prev;

        if (buddy < block)
            block = buddy;

        block->kval++;
    }

    // Add the block back to the free list
    struct avail *sentinel = &pool->avail[block->kval];
    block->next = sentinel->next;
    block->prev = sentinel;
    sentinel->next->prev = block;
    sentinel->next = block;
}
  

/**
 * @brief This is a simple version of realloc.
 *
 * @param poolThe memory pool
 * @param ptr  The user memory
 * @param size the new size requested
 * @return void* pointer to the new user memory
 */
void *buddy_realloc(struct buddy_pool *pool, void *ptr, size_t size)
{
    if (!pool)
        return NULL;
    if (!ptr)
        return buddy_malloc(pool, size);
    if (size == 0) {
        buddy_free(pool, ptr);
        return NULL;
    }

    // Recover the block header from the user pointer
    struct avail *block = (struct avail *)((char *)ptr - sizeof(struct avail));
    size_t allocated = ((size_t)1 << block->kval);
    size_t old_payload = allocated - sizeof(struct avail);

    // Calculate the min size that would require a smaller block
    size_t min_req = 0;
    if (block->kval > 0) {
        min_req = ((size_t)1 << (block->kval - 1)) - sizeof(struct avail) + 1;
    } else {
        min_req = 0;
    }

    if (size > min_req) {
        void *new_ptr = buddy_malloc(pool, size);
        if (!new_ptr) {
            return NULL; // Allocation failed
        }

        // Copy data from the old block to the new block
        size_t copy_size = (old_payload < size) ? old_payload : size;
        memcpy(new_ptr, ptr, copy_size);
        buddy_free(pool, ptr);
        return new_ptr;
    } else {
        // If the current block is suffcient return it
        return ptr;
    }
}


void buddy_init(struct buddy_pool *pool, size_t size)
{
    size_t kval = 0;
    if (size == 0)
        kval = DEFAULT_K;
    else
        kval = btok(size);

    if (kval < MIN_K)
        kval = MIN_K;
    if (kval > MAX_K)
        kval = MAX_K - 1;

    //make sure pool struct is cleared out
    memset(pool,0,sizeof(struct buddy_pool));
    pool->kval_m = kval;
    pool->numbytes = (UINT64_C(1) << pool->kval_m);
    //Memory map a block of raw memory to manage
    pool->base = mmap(
        NULL,                               /*addr to map to*/
        pool->numbytes,                     /*length*/
        PROT_READ | PROT_WRITE,             /*prot*/
        MAP_PRIVATE | MAP_ANONYMOUS,        /*flags*/
        -1,                                 /*fd -1 when using MAP_ANONYMOUS*/
        0                                   /* offset 0 when using MAP_ANONYMOUS*/
    );
    if (MAP_FAILED == pool->base)
    {
        handle_error_and_die("buddy_init avail array mmap failed");
    }

    //Set all blocks to empty. We are using circular lists so the first elements just point
    //to an available block. Thus the tag, and kval feild are unused burning a small bit of
    //memory but making the code more readable. We mark these blocks as UNUSED to aid in debugging.
    for (size_t i = 0; i <= kval; i++)
    {
        pool->avail[i].next = pool->avail[i].prev = &pool->avail[i];
        pool->avail[i].kval = i;
        pool->avail[i].tag = BLOCK_UNUSED;
    }

    //Add in the first block
    pool->avail[kval].next = pool->avail[kval].prev = (struct avail *)pool->base;
    struct avail *m = pool->avail[kval].next;
    m->tag = BLOCK_AVAIL;
    m->kval = kval;
    m->next = m->prev = &pool->avail[kval];
}

void buddy_destroy(struct buddy_pool *pool)
{
    int rval = munmap(pool->base, pool->numbytes);
    if (-1 == rval)
    {
        handle_error_and_die("buddy_destroy avail array");
    }
    //Zero out the array so it can be reused it needed
    memset(pool,0,sizeof(struct buddy_pool));
}

#define UNUSED(x) (void)x

