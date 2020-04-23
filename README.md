# Memory-Allocation-Package

Hello!

- This repository houses a general pupose dynamic storage allocator that I have programmed in C
- The allocator was able to achieve a 68.8% space utilization using a carefully designed coalescing algorithm

### Allocator Structure
Both allocated and free block share the same header structure
HEADER : 8-bytes, aligned to the 8th of a 16 byte aligned heap, where:
         - LSB is set to 1 when the block is allocated,
           0, otherwise
         - The 2nd LSB is set to 1 if the previous block is allocated,
           0, otherwise
         - The whole 8-byte value with the least significant bit set to 
           0 represents the size of the block as a size_t 

HEADER : 8-byte, aligned the 0th of a 16-byte aligned heap. Allocated 
         blocks DO NOT have footers, but free blocks do, reflecting 
         the same structure as a header.

- Minimum block size is 32 bytes.

#### ALLOCATED BLOCK STRUCTURE:
HEADER - as defined above
PAYLOAD - Memory allocated for programs to store information
SIZE - Payload size + 8 bytea(header)

##### Block Visualization.                                                      
                 block     block+8            block+size  
Allocated blocks:   |  HEADER  |  ... PAYLOAD ...  | 


#### FREE BLOCK STRUCTURE:
HEADER - as defined above
PAYLOAD - Memory allocated for programs to store information
FOOTER - same as header structure, but does not contain allocation 
         status of the previous block
SIZE - Min of 32 bytes
- Each free block also contains prev and next pointers to the next 
  and previous free blocks, therefore each pointer is 8 bytes with 
  a header and footer of 8 bytes each, giving a total of 32 bytes

##### Block Visualization:    
                  block      block+8        block+size-8   block+size    
Unallocated blocks: |  HEADER  |  ... (empty) ...  |  FOOTER  | 

#### INITIALIZATION                                                 

The following visualization reflects the beginning of the heap.           
   start            start+8           start+16                           
INIT: | PROLOGUE_FOOTER | EPILOGUE_HEADER |                               
PROLOGUE_FOOTER: 8-byte footer, as defined above, that simulates the      
                 end of an allocated block. Also serves as padding.      
EPILOGUE_HEADER: 8-byte block indicating the end of the heap.             
                It simulates the beginning of an allocated block         
                The epilogue header is moved when the heap is extended. 

- In the epilogue header, it is also specified that the previous block is 
 allocated, as initially there are no free blocks 

 #### BLOCK ALLOCATION & DEALLOCATION                                                

- Upon memory request of size S, a block of size S + wsize, rounded up 
 to 16 bytes, is allocated on the heap, where wsize is 8 bytes.
- If the size is <= 24, then the block size is rounded up to 32 
(as that is the minimum block size). 
- Selecting the block is done by finding an appropriate sized block from 
 the segregated list of free lists 
- A segregated list is a list of free lists where each list containts 
 free blocks of a particular size 
- So an array of free lists is maintained where each index in that array
 points to a free list containing blocks of a particular size bucket
- To search for a block, we search for an index that the block size gets 
 mapped to. For example, a block size of 10 bytes, may get mapped to 
 index 2 of the array. The index contains a pointer to a free list
- We then iterate through that free list to see if we find a free block 
 of the size we want using a first fit approach.
- If we dont find a block, we then go the next index of the array of 
 free lists to check of a free block. If we find a block, we then 
 allocate it, if we dont find a block in any of the free lists we 
 then return NULL.
- Since we havent found a block, we then increase the heap by 
 max(blocksize, chunksize) by calling mem_sbrk and we search again

- When we allocate a block, we signal the next block, that the current 
 block is allocated, so the prev_alloc bit (2nd LSB) in the next block 
 is set When we free the current block, we signal the next block that 
 the current block is free, so the prev_alloc bit (2nd LSB) in the next 
 block is cleared
- When we free a block, we find the index to which its size maps to and
then we add it to that free list

#### IMPROVING UTILIZATION  
- To improve use of the memory, we implement something called 'coalescing'
 which makes sure that at no given time there are two adjecent free blocks
 on the heap my joining the free blocks into one block


### Interface 

```void *malloc (size_t size)```
Allocates a block of memory and returns a pointer to the starting point of the block

```void free (void *ptr)``` 
Frees the block of memory given by the ptr to the start of the memory block

```void *realloc(void *ptr, size_t size)```
Returns a pointer to an allocated region of at least size bytes

```void *calloc (size_t nmemb, size_t size)```
Returns the a pointer to the newly allocated block of memory that is intialized to 0

```bool mm_init(void)```
Intialized the heap that will store all the allocated memory

