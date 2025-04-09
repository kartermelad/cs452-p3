#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "harness/unity.h"
#include "../src/lab.h"


void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}



/**
 * Check the pool to ensure it is full.
 */
void check_buddy_pool_full(struct buddy_pool *pool)
{
  //A full pool should have all values 0-(kval-1) as empty
  for (size_t i = 0; i < pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }

  //The avail array at kval should have the base block
  assert(pool->avail[pool->kval_m].next->tag == BLOCK_AVAIL);
  assert(pool->avail[pool->kval_m].next->next == &pool->avail[pool->kval_m]);
  assert(pool->avail[pool->kval_m].prev->prev == &pool->avail[pool->kval_m]);

  //Check to make sure the base address points to the starting pool
  //If this fails either buddy_init is wrong or we have corrupted the
  //buddy_pool struct.
  assert(pool->avail[pool->kval_m].next == pool->base);
}

/**
 * Check the pool to ensure it is empty.
 */
void check_buddy_pool_empty(struct buddy_pool *pool)
{
  //An empty pool should have all values 0-(kval) as empty
  for (size_t i = 0; i <= pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }
}

/**
 * Test allocating 1 byte to make sure we split the blocks all the way down
 * to MIN_K size. Then free the block and ensure we end up with a full
 * memory pool again
 */
void test_buddy_malloc_one_byte(void)
{
  fprintf(stderr, "->Test allocating and freeing 1 byte\n");
  struct buddy_pool pool;
  int kval = MIN_K;
  size_t size = UINT64_C(1) << kval;
  buddy_init(&pool, size);
  void *mem = buddy_malloc(&pool, 1);
  //Make sure correct kval was allocated
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests the allocation of one massive block that should consume the entire memory
 * pool and makes sure that after the pool is empty we correctly fail subsequent calls.
 */
void test_buddy_malloc_one_large(void)
{
  fprintf(stderr, "->Testing size that will consume entire memory pool\n");
  struct buddy_pool pool;
  size_t bytes = UINT64_C(1) << MIN_K;
  buddy_init(&pool, bytes);

  //Ask for an exact K value to be allocated. This test makes assumptions on
  //the internal details of buddy_init.
  size_t ask = bytes - sizeof(struct avail);
  void *mem = buddy_malloc(&pool, ask);
  assert(mem != NULL);

  //Move the pointer back and make sure we got what we expected
  struct avail *tmp = (struct avail *)mem - 1;
  assert(tmp->kval == MIN_K);
  assert(tmp->tag == BLOCK_RESERVED);
  check_buddy_pool_empty(&pool);

  //Verify that a call on an empty tool fails as expected and errno is set to ENOMEM.
  void *fail = buddy_malloc(&pool, 5);
  assert(fail == NULL);
  assert(errno == ENOMEM);

  //Free the memory and then check to make sure everything is OK
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests to make sure that the struct buddy_pool is correct and all fields
 * have been properly set kval_m, avail[kval_m], and base pointer after a
 * call to init
 */
void test_buddy_init(void)
{
  fprintf(stderr, "->Testing buddy init\n");
  //Loop through all kval MIN_k-DEFAULT_K and make sure we get the correct amount allocated.
  //We will check all the pointer offsets to ensure the pool is all configured correctly
  for (size_t i = MIN_K; i <= DEFAULT_K; i++)
    {
      size_t size = UINT64_C(1) << i;
      struct buddy_pool pool;
      buddy_init(&pool, size);
      check_buddy_pool_full(&pool);
      buddy_destroy(&pool);
    }
}

void test_btok(void)
{
    fprintf(stderr, "->Testing btok function\n");

    TEST_ASSERT_EQUAL(0, btok(0));
    TEST_ASSERT_EQUAL(0, btok(1));
    TEST_ASSERT_EQUAL(1, btok(2));
    TEST_ASSERT_EQUAL(2, btok(3));
    TEST_ASSERT_EQUAL(2, btok(4));
    TEST_ASSERT_EQUAL(3, btok(5));
    TEST_ASSERT_EQUAL(10, btok(1024)); 
    TEST_ASSERT_EQUAL(20, btok(1048576));
}

void test_buddy_calc(void)
{
    fprintf(stderr, "->Testing buddy_calc function\n");

    struct buddy_pool pool;
    size_t size = UINT64_C(1) << 5;
    buddy_init(&pool, size);

    struct avail *base = (struct avail *)pool.base;

    struct avail *block = base;
    block->kval = 3;

    struct avail *buddy = buddy_calc(&pool, block);

    size_t offset = (size_t)((char *)block - (char *)pool.base);
    size_t expected_offset = offset ^ (1 << block->kval);
    struct avail *expected_buddy = (struct avail *)((char *)pool.base + expected_offset);

    TEST_ASSERT_EQUAL_PTR(expected_buddy, buddy);

    buddy_destroy(&pool);
}

void test_buddy_malloc_multiple_small(void)
{
    fprintf(stderr, "->Testing multiple small allocations\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, 1);
    void *block2 = buddy_malloc(&pool, 1);
    void *block3 = buddy_malloc(&pool, 1);

    assert(block1 != NULL);
    assert(block2 != NULL);
    assert(block3 != NULL);
    assert(block1 != block2 && block2 != block3 && block1 != block3);

    buddy_free(&pool, block1);
    buddy_free(&pool, block2);
    buddy_free(&pool, block3);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_free_and_reallocate(void)
{
    fprintf(stderr, "->Testing freeing and reallocating\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, 1);
    assert(block1 != NULL);

    buddy_free(&pool, block1);

    void *block2 = buddy_malloc(&pool, 1);
    assert(block2 != NULL);
    assert(block1 == block2);

    buddy_free(&pool, block2);
    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_malloc_different_sizes(void)
{
    fprintf(stderr, "->Testing allocations of different sizes\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, 1);
    void *block2 = buddy_malloc(&pool, 16);
    void *block3 = buddy_malloc(&pool, 64);

    assert(block1 != NULL);
    assert(block2 != NULL);
    assert(block3 != NULL);

    buddy_free(&pool, block1);
    buddy_free(&pool, block2);
    buddy_free(&pool, block3);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_exhaust_pool(void)
{
    fprintf(stderr, "->Testing pool exhaustion\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, pool_size - sizeof(struct avail));
    assert(block1 != NULL);

    void *block2 = buddy_malloc(&pool, 1);
    assert(block2 == NULL);
    assert(errno == ENOMEM);

    buddy_free(&pool, block1);
    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_realloc(void)
{
    fprintf(stderr, "->Testing buddy_realloc\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block = buddy_malloc(&pool, 16);
    assert(block != NULL);

    void *larger_block = buddy_realloc(&pool, block, 32);
    assert(larger_block != NULL);
    assert(larger_block != block);

    void *smaller_block = buddy_realloc(&pool, larger_block, 8);
    assert(smaller_block != NULL);

    void *freed_block = buddy_realloc(&pool, smaller_block, 0);
    assert(freed_block == NULL);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_invalid_inputs(void)
{
    fprintf(stderr, "->Testing invalid inputs\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block = buddy_malloc(NULL, 16);
    assert(block == NULL);

    block = buddy_malloc(&pool, 0);
    assert(block == NULL);

    buddy_free(&pool, NULL);

    buddy_free(NULL, block);

    buddy_destroy(&pool);
}

void test_buddy_allocate_and_free_all(void)
{
    fprintf(stderr, "->Testing allocate and free all blocks\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *blocks[pool_size / sizeof(struct avail)];
    size_t i = 0;

    // Allocate all blocks
    while ((blocks[i] = buddy_malloc(&pool, 1)) != NULL) {
        i++;
    }

    // Free all blocks
    for (size_t j = 0; j < i; j++) {
        buddy_free(&pool, blocks[j]);
    }

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_random_allocations(void)
{
    fprintf(stderr, "->Testing random allocations and frees\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *blocks[100];
    size_t block_count = 0;

    for (int i = 0; i < 1000; i++) {
        if (rand() % 2 == 0 && block_count < 100) {
            // Allocate a random size
            size_t size = (rand() % (pool_size / 4)) + 1;
            void *block = buddy_malloc(&pool, size);
            if (block) {
                blocks[block_count++] = block;
            }
        } else if (block_count > 0) {
            // Free a random block
            size_t index = rand() % block_count;
            buddy_free(&pool, blocks[index]);
            blocks[index] = blocks[--block_count];
        }
    }

    // Free remaining blocks
    for (size_t i = 0; i < block_count; i++) {
        buddy_free(&pool, blocks[i]);
    }

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_coalescing(void)
{
    fprintf(stderr, "->Testing coalescing of buddy blocks\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, pool_size / 2 - sizeof(struct avail));
    void *block2 = buddy_malloc(&pool, pool_size / 2 - sizeof(struct avail));

    assert(block1 != NULL);
    assert(block2 != NULL);

    buddy_free(&pool, block1);
    buddy_free(&pool, block2);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_alignment(void)
{
    fprintf(stderr, "->Testing memory alignment\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    for (size_t size = 1; size <= pool_size / 2; size *= 2) {
        void *block = buddy_malloc(&pool, size);
        assert(block != NULL);
        assert(((uintptr_t)block & (size - 1)) == 0); // Check alignment
        buddy_free(&pool, block);
    }

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_fragmentation(void)
{
    fprintf(stderr, "->Testing fragmentation handling\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block1 = buddy_malloc(&pool, 1);
    void *block2 = buddy_malloc(&pool, 1);
    void *block3 = buddy_malloc(&pool, 1);

    buddy_free(&pool, block2); // Create a gap
    void *block4 = buddy_malloc(&pool, 1); // Should reuse the gap
    assert(block4 == block2);

    buddy_free(&pool, block1);
    buddy_free(&pool, block3);
    buddy_free(&pool, block4);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_large_allocation(void)
{
    fprintf(stderr, "->Testing allocation larger than pool size\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *block = buddy_malloc(&pool, pool_size + 1); // Too large
    assert(block == NULL);
    assert(errno == ENOMEM);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_stress(void)
{
    fprintf(stderr, "->Stress testing random allocations and frees\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    void *blocks[1000];
    size_t block_count = 0;

    for (int i = 0; i < 10000; i++) {
        if (rand() % 2 == 0 && block_count < 1000) {
            size_t size = (rand() % (pool_size / 4)) + 1;
            void *block = buddy_malloc(&pool, size);
            if (block) {
                blocks[block_count++] = block;
            }
        } else if (block_count > 0) {
            size_t index = rand() % block_count;
            buddy_free(&pool, blocks[index]);
            blocks[index] = blocks[--block_count];
        }
    }

    for (size_t i = 0; i < block_count; i++) {
        buddy_free(&pool, blocks[i]);
    }

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_edge_cases(void)
{
    fprintf(stderr, "->Testing smallest and largest allocations\n");
    struct buddy_pool pool;
    size_t pool_size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, pool_size);

    // Smallest allocation
    void *small_block = buddy_malloc(&pool, 1);
    assert(small_block != NULL);
    buddy_free(&pool, small_block);

    // Largest allocation
    void *large_block = buddy_malloc(&pool, pool_size - sizeof(struct avail));
    assert(large_block != NULL);
    buddy_free(&pool, large_block);

    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}


int main(void) {
  time_t t;
  unsigned seed = (unsigned)time(&t);
  fprintf(stderr, "Random seed:%d\n", seed);
  srand(seed);
  printf("Running memory tests.\n");

  UNITY_BEGIN();
  RUN_TEST(test_buddy_edge_cases);
  RUN_TEST(test_buddy_stress);
  RUN_TEST(test_buddy_large_allocation);
  RUN_TEST(test_buddy_fragmentation);
  RUN_TEST(test_buddy_coalescing);
  RUN_TEST(test_buddy_random_allocations);
  RUN_TEST(test_buddy_allocate_and_free_all);
  RUN_TEST(test_buddy_invalid_inputs);
  RUN_TEST(test_buddy_realloc);
  RUN_TEST(test_buddy_exhaust_pool);
  RUN_TEST(test_buddy_malloc_different_sizes);
  RUN_TEST(test_buddy_free_and_reallocate);
  RUN_TEST(test_buddy_malloc_multiple_small);
  RUN_TEST(test_buddy_calc);
  RUN_TEST(test_btok);
  RUN_TEST(test_buddy_init);
  RUN_TEST(test_buddy_malloc_one_byte);
  RUN_TEST(test_buddy_malloc_one_large);
  return UNITY_END();
}
