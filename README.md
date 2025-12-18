## Dynamic memory allocator

This project implements a custom dynamic memory allocator in C, replicating the functionality of the standard `malloc` and `free`. It manages a heap memory pool using an implicit free list structure with specific optimizations for memory utilization.

### Key Features

* **Implicit Free List**: Uses a linear heap structure to track allocated and free blocks.
* **First Fit Policy**: Searches for the first available free block that fits the requested size.
* **Immediate Coalescing**: Merges adjacent free blocks immediately upon freeing to minimize external fragmentation.
* **Boundary Tag Optimization (Footer Removal)**: Allocated blocks do not store a footer (boundary tag). This saves 4 bytes (on 32-bit) or 8 bytes (on 64-bit) per allocation, improving memory utilization.
* **2-Bit Tagging System**: The block header uses the lowest 2 bits to store allocation status:
    * **Bit 0**: Current block allocation status (0: Free, 1: Allocated).
    * **Bit 1**: Previous block allocation status (0: Free, 1: Allocated).

### File Structure

* `malloc.c`: Main source code containing the allocator implementation and a test driver (`main` function).

### Functions

* `mm_init()`: Initializes the heap model, creating prologue and epilogue blocks.
* `mm_alloc(size_t size)`: Allocates a block of at least `size` bytes.
* `mm_free(void *ptr)`: Frees the block pointed to by `ptr` and coalesces it with neighbors if possible.
* `show_mm()`: A utility function that traverses the heap and prints the status of every block (header, footer, size, flags) for debugging purposes.

### How to Compile and Run

This project can be compiled using `gcc`.

### Build

```bash
gcc -o malloc malloc.c
```

### Run

```bash
./malloc
```
