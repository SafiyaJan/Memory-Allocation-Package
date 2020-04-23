
   /*
   ************************************************************************
                                     mm.c                              
            64-bit Struct-Based Segregated Free List Memory Allocator        
                   15-213: Introduction to Computer Systems
                       CMUQ Fall 2017 - Safiya Jan                                                                                     
   ************************************************************************  
 
                                                                     
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want debugging output, uncomment the following.  Be sure not
 * to have debugging enabled in your final submission
 */
//#define DEBUG

#ifdef DEBUG
/* When debugging is enabled, the underlying functions get called */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__) 
#define dbg_checkheap(...) mm_checkheap(__VA_ARGS__)
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated */
#define dbg_printf(...)
#define dbg_assert(...)
#define dbg_requires(...)
#define dbg_ensures(...)
#define dbg_checkheap(...)
#define dbg_printheap(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* What is the correct alignment? */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
static size_t align(size_t x) {
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disnabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Basic constants */
typedef uint64_t word_t;
static const size_t wsize = sizeof(word_t);   // word, header, footer size (bytes)
static const size_t dsize = 2*wsize;          // double word size (bytes)
static const size_t min_block_size = 2*dsize; // Minimum block size
static const size_t chunksize = (1 << 12);    // requires (chunksize % 16 == 0)

typedef struct block
{
    /* Header contains size + allocation flag */
    word_t header;
    /*
     * We don't know how big the payload will be.  Declaring it as an
     * array of size 0 allows computing its starting address using
     * pointer notation.
     */
    char payload[0];
    /*
     * We can't declare the footer as part of the struct, since its starting
     * position is unknown
     */
} block_t;


/* Global variables */

/* Size of segregated free list array */
#define LIMIT 17
/* Pointer to first block */
static block_t *heap_listp = NULL;
/* Pointer to array of seg list */
static block_t *free_listp[LIMIT];
/* Pointer to array of pointers to the back of each free_list */
static block_t *free_back[LIMIT];


/* Function prototypes for internal helper routines */
static block_t *extend_heap(size_t size);
static void place(block_t *block, size_t asize);
static block_t *find_fit(size_t asize);
static block_t *coalesce(block_t *block);

static size_t max(size_t x, size_t y);
static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc);

static size_t extract_size(word_t header);
static size_t get_size(block_t *block);
static size_t get_payload_size(block_t *block);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);

static void write_header(block_t *block, size_t size, bool alloc);
static void write_footer(block_t *block, size_t size, bool alloc);
static void write_header_new(block_t *block, size_t size, bool alloc, bool alloc_prev);
static void write_footer_new(block_t *block, size_t size, bool alloc, bool alloc_prev);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

static size_t free_index(size_t asize);
void static add_free_block(block_t* block);
void static remove_free_block(block_t* block);
static block_t *get_prev(block_t* block);
static block_t *get_next(block_t* block);

static bool get_prev_alloc(block_t *block);
static void set_prev_alloc(block_t *block, bool alloc);

static bool correct_block(block_t *block);
bool mm_checkheap(int lineno);


/*
 * mm_init: initializes the heap; it is run once when heap_start == NULL.
 *          prior to any extend_heap operation, this is the heap:
 *              start            start+8           start+16
 *          INIT: | PROLOGUE_FOOTER | EPILOGUE_HEADER |
 * heap_listp ends up pointing to the epilogue header.
 */
bool mm_init(void) 
{

    size_t index;
    
    // Initializing segregated free list array 
    for (index = 0; index < LIMIT; index++) {
        free_listp[index] = NULL;
    }
    // Initializing back pointers to each each free list
    for (index = 0; index < LIMIT; index++) {
        free_back[index] = NULL;
    }

    // Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2*wsize));

    if (start == (void *)-1) 
    {
        return false;
    }

    start[0] = pack(0, true); // Prologue footer
    start[1] = (pack(0, true))|0x2; // Epilogue header

    // Heap starts with first block header (epilogue)
    heap_listp = (block_t *) &(start[1]);
    
    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize/dsize) == NULL)
    {
        return false;
    }

    return true;

}

/*
 * malloc: allocates a block with size at least (size + wsize), rounded up to
 *         the nearest 16 bytes, with a minimum of 2*dsize. Seeks a
 *         sufficiently-large unallocated block on the heap to be allocated.
 *         If no such block is found, extends heap by the maximum between
 *         chunksize and (size + wsize) rounded up to the nearest 16 bytes,
 *         and then attempts to allocate all, or a part of, that memory.
 *         Returns NULL on failure, otherwise returns a pointer to such block.
 *         The allocated block will not be used for further allocations until
 *         freed.
 */
void *malloc (size_t size) 
{
 
    size_t asize; //Adjusted block size
    size_t extendsize; //Amount to extend heap if no fit found
    block_t *block;
    void *bp = NULL;

    if (heap_listp == NULL) // Initialize heap if it isn't initialized
    {
        mm_init();
    }

    if (size == 0) // Ignore spurious request
    {
        return bp;
    }

    // Adjust block size to include overhead (header) and to meet alignment requirements
    if (size <= 24)
        asize = 32;
    else 
        asize = round_up(size+8,16); 
  
    // Search the free list for a fit
    block = find_fit(asize);

    if (block == NULL)
    {
        extendsize = max(asize, chunksize);
        block = extend_heap(extendsize);
        if (block == NULL) // extend_heap returns an error
        {
            return bp;
        }
    }

    place(block, asize);
    bp = header_to_payload(block);
    return bp;
}

/*
 * free: Frees the block such that it is no longer allocated while still
 *       maintaining its size. Block will be available for use on malloc.
 *       Creates a new header footer for the free block, including the allocation
 *       status of the previous block. Then set the allocation bit of the next block 
 *       to 0
 */
void free (void *ptr) 
{
    
    if (ptr == NULL)
    {
        return;
    }

    block_t *block = payload_to_header(ptr);
    size_t size = get_size(block);

    //Getting the alloc status of the previous block
    bool prev_alloc = get_prev_alloc(block);
    
    //Making sure the new free block reflects the status of the previous block
    write_header_new(block, size, false, prev_alloc);
    write_footer_new(block, size, false, prev_alloc);

    //Setting the next block prev alloc tag to false
    block_t* next = find_next(block);
    set_prev_alloc(next, false);
    
    coalesce(block);

    return;

}

/*
 * realloc: returns a pointer to an allocated region of at least size bytes:
 *          if ptrv is NULL, then call malloc(size);
 *          if size == 0, then call free(ptr) and returns NULL;
 *          else allocates new region of memory, copies old data to new memory,
 *          and then free old block. Returns old block if realloc fails or
 *          returns new pointer on success.
 */
void *realloc(void *oldptr, size_t size) 
{
    block_t *block = payload_to_header(oldptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0)
    {
        free(oldptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (oldptr == NULL)
    {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);
    // If malloc fails, the original block is left untouched
    if (!newptr)
    {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if(size < copysize)
    {
        copysize = size;
    }
    memcpy(newptr, oldptr, copysize);

    // Free the old block
    free(oldptr);

    return newptr;
}

/*
 * calloc: Allocates a block with size at least (elements * size + dsize)
 *         through malloc, then initializes all bits in allocated memory to 0.
 *         Returns NULL on failure.
 */
void *calloc (size_t nmemb, size_t size) {
    void *bp;
    size_t asize = nmemb * size;

    if (asize/nmemb != size)
    // Multiplication overflowed
    return NULL;
    
    bp = malloc(asize);
    if (bp == NULL)
    {
        return NULL;
    }
    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/********** START OF HELPER FUNCTIONS *********/

/*
 * extend_heap: Extends the heap with the requested number of bytes, and
 *              recreates epilogue header. Returns a pointer to the result of
 *              coalescing the newly-created block with previous free block, if
 *              applicable, or NULL in failure.
 */
static block_t *extend_heap(size_t size) 
{
    void *bp;
    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);

    if ((bp = mem_sbrk(size)) == (void *)-1)
    {
        return NULL;
    }
    
    // Initialize free block header/footer 
    block_t *block = payload_to_header(bp);
    write_header_new(block, size, false, true);
    write_footer_new(block, size, false, true);

    // Create new epilogue header
    block_t *block_next = find_next(block);
    write_header_new(block_next, 0, true, false);

    // Coalesce in case the previous block was free
    return coalesce(block);
}


/* Coalesce: Coalesces current block with previous and next blocks if
 *           either or both are unallocated; otherwise the block is not
 *           modified. Then, insert coalesced block into the segregated list.
 *           Returns pointer to the coalesced block. After coalescing, the
 *           immediate contiguous previous and next blocks must be allocated.
 */
static block_t *coalesce(block_t * block) 
{
    
    block_t *block_next = find_next(block);

    // Find the allocation satus of the prev block
    bool prev_alloc = get_prev_alloc(block);
    bool next_alloc = get_alloc(block_next);

    // If the previous block is not allocated, then the prev block has a footer
    // and we can find the previous block
    block_t *block_prev = NULL;
    if (prev_alloc==false)
        block_prev = find_prev(block);

    size_t size = get_size(block);
    
    /* Case 1 - Next and previous blocks are allocated, just add the block 
       to a free list */
    if (prev_alloc && next_alloc)              
    {
        
        add_free_block(block);
        return block;
    }
    /* Case 2 - Prev block is allocated, next block is free */
    else if (prev_alloc && !next_alloc)       
    {
        size += get_size(block_next);
        remove_free_block(block_next);
        write_header_new(block, size, false, true);
        write_footer_new(block, size, false, true);
        add_free_block(block);
    }
    /* Case 3 - Prev block is free and next block is allocated */
    else if (!prev_alloc && next_alloc)        
    {
        //getting alloc status of prev block of prev block 
        bool prev_alloc_1 = get_prev_alloc(block_prev);

        size += get_size(block_prev);
        
        remove_free_block(block_prev);
    
        write_header_new(block_prev, size, false, prev_alloc_1);
        write_footer_new(block, size, false, prev_alloc_1);

        block = block_prev;
        add_free_block(block); 
    }
    
    /* Case 4 - next and prev blocks are free */
    else                                       
    {
        //getting the alloc status of prev block of prev block
        bool prev_alloc_1 = get_prev_alloc(block_prev);

        size += get_size(block_next) + get_size(block_prev);

        remove_free_block(block_prev);
        remove_free_block(block_next);
        write_header_new(block_prev, size, false, prev_alloc_1);
        write_footer_new(block_next, size, false, prev_alloc_1);
        
        block = block_prev;
        add_free_block(block);
        
    }
    return block;
}

/*
 * place: Places block with size of asize at the start of bp. If the remaining
 *        size is at least the minimum block size, then split the block to the
 *        the allocated block and the remaining block as free, which is then
 *        inserted into the segregated list. Requires that the block is
 *        initially unallocated.
 */
static void place(block_t *block, size_t asize)
{
   

    size_t csize = get_size(block);

    if ((csize - asize) >= min_block_size)
    {
        //Get state of prev block
        bool prev_alloc = get_prev_alloc(block);
        
        remove_free_block(block);

        //Writing only the header of the new allocated block
        write_header_new(block, asize, true, prev_alloc);

        //Getting the next alloc block
        block = find_next(block);
        
        //Writing the header and footer of the new free block
        write_header_new(block, csize-asize, false, true);
        write_footer_new(block, csize-asize, false, true);
        
        //Adding the remaing part of the block to the free list
        add_free_block(block);

    }
    /* Come here if exact size is found */
    else
    {
        bool prev_alloc = get_prev_alloc(block);
        write_header_new(block, csize, true, prev_alloc);
        remove_free_block(block);
    }
}

/*
 * find_fit: Looks for a free block with at least asize bytes with
 *           first-fit policy. Returns NULL if none is found.
 */
static block_t *find_fit(size_t asize)
{
    // Find the index at which the block might exist 
    size_t index = free_index(asize);
    block_t * block;
    
    /* Starting from index iterate through the each free list of the segregated 
    list to find the block */
    for(size_t i = index; i < LIMIT; i++)
    {
      for (block = free_listp[i]; block!=NULL ; block = get_next(block))
      {
        if (asize <= get_size(block))
        {

            return block;
        }
      }
    }
    return NULL; // no fit found

}

/*
 * add_free_block: Adds the free block to appropriate free list. Free block are added 
 *                  to the back of each free list 
 */                  
void static add_free_block(block_t* block)
{
    // Finding the free_list to which to add the free block
    size_t index = free_index(get_size(block));
    
    word_t* temp = (word_t*)block;
    word_t* temp_free = (word_t*)free_back[index];
    
    // Specifying the next block that the current block is free
    block_t* next = find_next(block);
    set_prev_alloc(next,false);

    /* When adding a free block for the first time, the next and prev block pointers 
       are NULL and the back and front pointers point to the newly added block */
    if (free_listp[index] == NULL && free_back[index] == NULL)
    {
        temp[2] = 0;
        temp[1] = 0;
        free_listp[index] = block;
        free_back[index] = block;
    }
    
    /* Else just add the block to the back of the list and let the back pointer point
       to the newly added block. Set the next pointer to NULL and set the prev pointer 
       to point to the old last block */
    else
    {
        temp[1] = (word_t)temp_free;
        temp[2] = 0;
        temp_free[2] = (word_t)block;
        free_back[index] = block;
    }

}

/*
 * remove_free_block: Removes the free block to appropriate free list. Blocks can
 *                    be removed anywhere from the free_list
 */
void static remove_free_block(block_t* block)
{
    // Finding the free_list to which to add the free block
    size_t index = free_index(get_size(block));

    // Findind the next and previous free blocks
    word_t* prev = (word_t*)(get_prev(block));
    word_t* next = (word_t*)(get_next(block));
    block_t* next_block = find_next(block);
    
    // Specifying the next block that the current block is allocated
    set_prev_alloc(next_block,true);
   
    // Prev is null if the block being removed is the first bloxk in the list
    if (prev == NULL)
    {
        // If the block is the first and only block in the list
        if (free_listp[index] == free_back[index])
        {
            free_listp[index] = (block_t*)next;
            free_back[index] = (block_t*)next;
            
            if (next!=NULL)
            {
                next[1] = 0;
            }

        }
        
        // If the block is the first, set the the prev pointer of the next block to NULL
        else 
        {
            free_listp[index] = (block_t*)next;
           
            if (next!=NULL)
            {
                next[1] = 0;
            }

        }
    }


    // If the block is the last block in the list, set the next pointer of the previous 
    // block to 0 and point the back pointer to the previous block
    else if ((prev!=NULL) && (next == NULL))
    {
        
        prev[2] = 0;
        free_back[index] = (block_t*)prev;

    }
    
    // If the block is in the middle of the list, let previous block point to the next block
    // and the next block's previous pointer point to the previous block
    else if ((prev!=NULL) && (next != NULL))
    {
        
        prev[2] = (word_t)next;

        if (next!=NULL)
        {
            next[1] = (word_t)prev;
        }
    }

    return;
}

/*
 * get_prev: Returns the previous free block from the current block
 */
static block_t * get_prev(block_t* block)
{
    word_t* addr = (word_t*)(block -> payload);
    return (block_t*)(word_t*)addr[0];

}


/*
 * get_prev: Returns the next free block from the current block
 */
static block_t *get_next(block_t* block)
{
    word_t* addr = (word_t*)(block -> payload);
    return (block_t*)(word_t*)addr[1];
}


/*
 * write_header_new: Writes the header of the current block with the allocation status
 *                   of the previous block specified by alloc_prev
 */
static void write_header_new(block_t *block, size_t size, bool alloc, bool alloc_prev)
    {
        bool prev_alloc = alloc_prev;
        block->header = pack(size, alloc);
        if (prev_alloc == true)
            block->header |= 1 << 1;
        if (prev_alloc == false)
        block->header &= ~(1 << 1);
    }

/*
 * write_header_new: Writes the footer of the current block with the allocation status
 *                   of the previous block specified by alloc_prev
 */    
static void write_footer_new(block_t *block, size_t size, bool alloc, bool alloc_prev)
{
   bool prev_alloc = alloc_prev;
   word_t *footerp = (word_t *)((block->payload) + get_size(block) - dsize); 
   *footerp = pack(size, alloc);
   if (prev_alloc == true)
        block->header |= 1 << 1;
    if (prev_alloc == false)
        block->header &= ~(1 << 1);
}

/*
 * set_prev_alloc: Set the allocation bit (2nd LSB) of the next block's header 
 *                 to alloc_prev
 */ 
static void set_prev_alloc(block_t* block, bool alloc_prev)
{
    bool prev_alloc = alloc_prev;
    if (prev_alloc == true)
        block->header |= 1 << 1;
    if (prev_alloc == false)
        block->header &= ~(1 << 1);
}

/*
 * get_prev_alloc: Retrieve the allocation status of the previous block 
 */ 
static bool get_prev_alloc(block_t* block)
{
    return (block->header & 0x2);
}

/*
 * free_index: Returns the index of segrgated free list array which 
 *             contains a free list containing blocks of asize <= size
 */
size_t free_index(size_t asize)
{
    if (asize <= 4)
        return 0;
    if (asize <= 32)
        return 1;
    if (asize <= 64)
        return 2;
    if (asize <= 128)
        return 3;
    if (asize <= 256)
        return 4;
    if (asize <= 512)
        return 5;
    if (asize <= 1024)
        return 6;
    if (asize <= 2048)
        return 7;
    if (asize <= 4096)
        return 8;
    if (asize <= 8192)
        return 9;
    if (asize <= 16384)
        return 10;
    if (asize <= 32768)
        return 11;
    if (asize <= 65536)
        return 12;
    if (asize <= 131072)
        return 13;
    if (asize <= 262144)
        return 14;
    if (asize <= 524288)
        return 15;
    else 
        return 16;
}

/*********** END OF STUDENT WRITTEN HELPER FUNCTIONS *********************/




/********** BASELINE GIVEN FUNCTIONS DO NOT CHANGE ***********************/

static size_t max(size_t x, size_t y)
{
    return (x > y) ? x : y;
}


/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n)
{
    return (n * ((size + (n-1)) / n));
}

/*
 * pack: returns a header reflecting a specified size and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise.
 */
static word_t pack(size_t size, bool alloc)
{
    return alloc ? (size | 1) : size;
}


/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word)
{
    return (word & ~(word_t) 0xF);
}

/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block)
{   
    //assert(block!=NULL);
    return extract_size(block->header);
}

/*
 * get_payload_size: returns the payload size of a given block, equal to
 *                   the entire block size minus the header and footer sizes.
 */
static word_t get_payload_size(block_t *block)
{
    size_t asize = get_size(block);
    return asize - wsize;
}

/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word)
{
    return (bool)(word & 0x1);
}

/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block)
{
    return extract_alloc(block->header);
}

/*
 * write_header: given a block and its size and allocation status,
 *               writes an appropriate value to the block header.
 */
static void write_header(block_t *block, size_t size, bool alloc)
{
    block->header = pack(size, alloc);
}

/*
 * write_footer: given a block and its size and allocation status,
 *               writes an appropriate value to the block footer by first
 *               computing the position of the footer.
 */
static void write_footer(block_t *block, size_t size, bool alloc)
{
    word_t *footerp = (word_t *)((block->payload) + get_size(block) - dsize);
    *footerp = pack(size, alloc);
}

/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block)
{
    return (block_t *)(((char *)block) + get_size(block));
}

/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block)
{
    // Compute previous footer position as one word before the header
    return (&(block->header)) - 1;
}

/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block)
{
    word_t *footerp = find_prev_footer(block);
    size_t size = extract_size(*footerp);
    return (block_t *)((char *)block - size);
}

/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp)
{
    return (block_t *)(((char *)bp) - offsetof(block_t, payload));
}

/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload.
 */
static void *header_to_payload(block_t *block)
{
    return (void *)(block->payload);
}

/************* END OF BASELINE GIVEN FUNCTIONS ************/




/************* DEBUGGING FUNCTIONS ************************/

/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */

static bool in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void *p) {
    size_t ip = (size_t) p;
    return align(ip) == ip;
}


/*
 * correct_ block - Checks if the block is in the heap and if its aligned
 *                  correctly
 */
static bool correct_block(block_t *block)
{
    bool inheap = in_heap((void*)block);
    //bool if_align = aligned((void*)block);
    if (!inheap)
    {
        printf("Block is out of heap bounds\n");
        return false;
    }
    if (aligned((void*)block))
    {
        printf("Block is not aligned\n");
        return false;
    }

    return true;
}

/*
 * mm_checkheap - Iterates through the heap and checks if each block is correct, 
 *                using the correct_block function
 *              - Checks if blocks are not coalesced
 *              - Checks if free blocks are correct or not using correct_block function
 *              - Iterates through all the free lists and checks if the LSB is set to 0  
 */
bool mm_checkheap(int lineno) {
    block_t* temp;
    block_t* block;

    // Iterating through heap checking if each block satisfies conditions
    for (temp = heap_listp; get_size(temp) > 0 && temp!=NULL; temp = find_next(temp))
    {
        if (!correct_block(temp))
        { 
            return false;
        }
        //Check if there are 2 consective free blocks - they are not coalesced
        if (get_alloc(temp) == 0 && get_alloc(find_next(temp)) == 0)
        {
            printf("Two consective blocks %p and %p are not coalesced \n",temp,find_next(temp));  
            return false;
        }
        // Checks if the block size is atleast 32
        if (get_size(temp)<32)
        {
            printf("Block size of block %p is less than 32\n", temp);
            return false;
        }

    }

    // Checking each free block has its alloc bit (LSB) in header set to 0
    for(size_t i = 0; i < LIMIT; i++)
    {
      for (block = free_listp[i]; block!=NULL ; block = get_next(block))
      {
        // Checking each free block has its alloc bit (LSB) in header set to 0
        if (get_alloc(block) == true)
        {
            return false;
        }
        // Checking if the block is within heap bounds 
        if (!correct_block(block))
        {
            return false;
        } 

      }
    }

    return true;

}
