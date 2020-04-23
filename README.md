# Memory-Allocation-Package

Hello!

I have created general purpose dynamic storage allocator in C. It implements the following functionalities:

### Allocates a block of memory with ```size```
```void *malloc (size_t size)```

### Deallocates a block of memory 
```void free (void *ptr)``` 

### Returns a pointer to an allocated region of at least ```size``` bytes 
```void *realloc(void *ptr, size_t size)```

### Allocates a block of memory with ```size``` & initializes the block with 0
```void *calloc (size_t nmemb, size_t size)```

### Initalizes the heap
```bool mm_init(void)```
