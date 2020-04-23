# Memory-Allocation-Package

Hello!

I have created general purpose dynamic storage allocator in C. It implements the following functionalities:


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
