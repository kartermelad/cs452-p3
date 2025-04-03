# Buddy Memory Allocator

This project implements a **buddy memory allocator**, a dynamic memory allocation system that efficiently manages memory by splitting and coalescing blocks of memory.

## Overview

The buddy memory allocator divides memory into blocks of sizes that are powers of two. It supports the following operations:
- **Allocation (`buddy_malloc`)**: Allocates a block of memory of the requested size.
- **Deallocation (`buddy_free`)**: Frees a previously allocated block and attempts to merge it with its buddy block if possible.
- **Reallocation (`buddy_realloc`)**: Resizes an allocated block, either by keeping it in place or reallocating to a larger block
The allocator uses a circular doubly linked list to manage free blocks and ensures efficient splitting and merging of memory blocks.

## Features

- **Efficient Memory Management**: Splits and coalesces blocks to minimize fragmentation.
- **Dynamic Resizing**: Supports resizing of allocated blocks with `buddy_realloc`.

## Building

To build the project, run:

```bash
make
```

## Testing

To run the test suite and verify the implementation, run:

```bash
make check
```

## Clean

To clean up the build files, run:

```bash
make clean
```

## Install Dependencies

To install any required dependencies (e.g., for testing or development), run:

```bash
make install-deps
```

## File Structure

- **`src/lab.c`**: Contains the implementation of the buddy memory allocator, including `buddy_malloc`, `buddy_free`, and `buddy_realloc`.
- **`tests/test-lab.c`**: Contains unit tests to verify the correctness of the allocator.
- **`Makefile`**: Automates the build, test, and clean processes.

## How It Works

1. **Initialization**:
   - The memory pool is initialized using `buddy_init`, which sets up the free list and maps a block of memory.

2. **Allocation**:
   - `buddy_malloc` finds the smallest available block that can satisfy the requested size. If necessary, larger blocks are split into smaller ones.

3. **Deallocation**:
   - `buddy_free` marks a block as free and attempts to merge it with its buddy block if the buddy is also free.

4. **Reallocation**:
   - `buddy_realloc` resizes a block by either keeping it in place or allocating a new block
  
## References
https://manpages.ubuntu.com/
