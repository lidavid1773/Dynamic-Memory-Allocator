/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include <stddef.h>
#include <errno.h>



// Helper functions --------------------------------------------------------------------------------------------------------
// Given a block, return the block size
size_t getBlockSize(sf_block* block){
    int mask = 0xFFFFFFFF;
    mask = mask << 5;
    return block->header & mask;
}

// Given a blocksize, return the index of the free list that would be able to satisfy a request of specified size.
int findFirstValidFreeList(size_t blockSize){
    // Blocksize is assumped to be a multiple of 32.
    int howManyM = blockSize / 32;
    if (howManyM == 1) return 0;
    else if (howManyM == 2) return 1;
    else if (howManyM == 3) return 2;
    else if (howManyM > 3 && howManyM <= 5) return 3;
    else if (howManyM > 5 && howManyM <= 8) return 4;
    else if (howManyM > 8 && howManyM <= 13) return 5;
    else return 6; // We stop here because, we only want to consider the wilderness block if this list is empty
}

// Given a block, return address to footer
sf_footer* getFooterAddress(sf_block* block){
    return (void*)block + getBlockSize(block) -8;
}

// Given an index representing one of the eight freelists, return 1 if the freelist is empty. 0, otherwise.
int listIsEmpty(int i){
    if (sf_free_list_heads[i].body.links.next == sf_free_list_heads[i].body.links.prev) return 1;
    return 0;
}

// Return 1 if splitting the block w/ a specific size will cause a splinter. 0, otherwise.
int splitWillSplinter(sf_block* block, size_t size){
    if (getBlockSize(block) - size < 32) return 1;
    return 0;
}

// Function splits block into the given size, and returns pointer to the newly created block (after splitting)
// The "lower part" (i.e. locations w/ lower-numbered addresses) is used to satisfy the allocation request.
// The "upper part" (i.e. locations w/ higher-numbered addresses) becomes the remainder.
sf_block* splitBlock(sf_block* block, size_t size){
    size_t originalBlockSize = getBlockSize(block);
    // Split the block by updating header to have new size and updating footer in the new block
    block->header = (size | THIS_BLOCK_ALLOCATED);
    sf_footer* blockFooter = getFooterAddress(block);
    *blockFooter = block->header;

    // Create a new block w/ proper header, footer
    sf_block* newBlock = (void*)block + getBlockSize(block);
    newBlock->header = originalBlockSize - getBlockSize(block);
    sf_footer* newBlockFooter = getFooterAddress(newBlock);
    *newBlockFooter = newBlock->header;

    return newBlock;
}

// Coalescing function for sf_mem_grow because an attempt to coalesce the newly allocated page from the function call should
//      be made w/ the wilderness block in order to build blocks larger than one page.
sf_block* coalesceBlockWithPage(sf_block* block){
    // Update block header to be new size
    block->header = getBlockSize(block) + PAGE_SZ;

    // Create new footer
    sf_footer* footerAddress = getFooterAddress(block);
    *footerAddress = block->header;

    return block;
}

// Coalescing function for two blocks (extend block1 to include block2)
sf_block* coalesceBlockWithBlock(sf_block* block1, sf_block* block2){
    // Cannot just blindly coalesce blocks. The new block always has to be the one that comes earlier in memory.
    if (block1 < block2){
    // Update block header to be new size
        block1->header = getBlockSize(block1) + getBlockSize(block2);

        // Create new footer
        sf_footer* footerAddress = getFooterAddress(block1);
        *footerAddress = block1->header;

        return block1;
    }
    else{
        // Update block header to be new size
        block2->header = getBlockSize(block1) + getBlockSize(block2);

        // Create new footer
        sf_footer* footerAddress = getFooterAddress(block2);
        *footerAddress = block2->header;

        return block2;
    }
}

// Given the index of an non-empty freelist, return pointer to first block in that list that is at least of size "size". NULL if none.
sf_block* getFirstFit(int i, size_t size){
    sf_block* firstNode = sf_free_list_heads[i].body.links.next;
    // Check if first node is large enough to satisfy request
    if (getBlockSize(firstNode) >= size){
        return firstNode;
    }

    // If not, repeat on the next nodes until the next node is the sentinel node. If next node is sentinel node, return NULL since we are at the end.
    sf_block* nextNode = firstNode->body.links.next;
    while (nextNode != &sf_free_list_heads[i]){
        if (getBlockSize(nextNode) >= size){
            return nextNode;
        }
        sf_block* nextNode = nextNode->body.links.next;
    }

    return NULL;
}

// Function that returns 1 if given block is free. Returns 0 otherwise.
int blockIsFree(sf_block* block){
    if (block->header == getBlockSize(block)) return 1;
    return 0;
}

// Function inserts the block to the front of the freelist at given index
void insertIntoList(sf_block* block, int index){
    if (index == NUM_FREE_LISTS-1){ // If we are dealing with the wilderness free block
        sf_free_list_heads[index].body.links.next = block;
        sf_free_list_heads[index].body.links.prev = block;

        block->body.links.next = &sf_free_list_heads[index];
        block->body.links.prev = &sf_free_list_heads[index];
    }
    else{ // Add block to the front of the list
        // Set pointers of block
        block->body.links.next = sf_free_list_heads[index].body.links.next;
        block->body.links.prev = &sf_free_list_heads[index];

        // Set pointers of the sentinel
        // The old first element's prev points to block
        sf_free_list_heads[index].body.links.next->body.links.prev = block;
        // The first element is now block
        sf_free_list_heads[index].body.links.next = block;
    }
}

// Takes in a block and removes it from its freelist.
void removeFromItsList(sf_block* block, int index){
    // If wilderness block
    if (index == NUM_FREE_LISTS-1){
        sf_free_list_heads[index].body.links.next = &sf_free_list_heads[index];
        sf_free_list_heads[index].body.links.prev = &sf_free_list_heads[index];
    }
    else{
        sf_block* previous = &sf_free_list_heads[index];
        sf_block* current = previous->body.links.next;
        while (current != block){
            previous = current;
            current = current->body.links.next;
        }
        // Remove
        previous->body.links.next = current->body.links.next;
        current->body.links.next->body.links.prev = previous;
    }

}

// Returns 1 if given block is wilderness block, 0 otherwise
int isWildernessBlock(sf_block* block){
    if (sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next == block) return 1;
    return 0;
}

// Returns 1 if given pointer is valid. 0, Otherwise.
int pointerIsValid(void *p){
    sf_block* block = (sf_block*)(p - sizeof(sf_header));

    // The follow cases are invalid pointers (call abort to exit the program)
    // The pointer is NULL
    if (p == NULL) return 0;

    // The pointer is not 32-byte aligned
    // if (((uintptr_t)pp - 8) % 32 != 0) return 0; // TODO / FIX ---------------------------------------------------------------------------------------

    // The block size is less than the minimum block size of 32
    if (getBlockSize(block) < 32) return 0;

    // The block size is not a multiple of 32
    if (getBlockSize(block) % 32 != 0) return 0;

    // The header of the block is before the start of the first block of the heap/ or the footer of the block is after the end of the last block in the heap
    // if (pp < sf_mem_start() || (void*)getFooterAddress(block) > sf_mem_end()) return 0; // TODO / FIX ---------------------------------------------------

    // The allocated bit in the header is 0
    if (block->header == getBlockSize(block)) return 0;

    return 1;
}

// -------------------------------------------------------------------------------------------------------------------------



/*
 * This is your implementation of sf_malloc. It acquires uninitialized memory that
 * is aligned and padded properly for the underlying system.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If size is 0, then NULL is returned without setting sf_errno.
 * If size is nonzero, then if the allocation is successful a pointer to a valid region of
 * memory of the requested size is returned.  If the allocation is not successful, then
 * NULL is returned and sf_errno is set to ENOMEM.
 */

void *sf_malloc(size_t size) {
    // Check if request size is 0. If so, return NULL without setting sf_errno.
    if (size == 0){
        return NULL;
    }

    // If heap has not been initialized (first call to sf_malloc).
    if (sf_mem_start() == sf_mem_end()){
        // Initialize the head of each free list in sf_free_list_heads by setting the next and prev pointers of the
        //      sentinel node to point back to the node itself.

        for (int i=0; i<NUM_FREE_LISTS; i++){
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }

        // Make a call to sf_mem_grow to obtain a page of memory within which to set up the prologue & epilogue w/ specified padding.
        // The remainder memory in this first page should then be inserted into the wilderness block as
        //      a single free block w/ normal header & footers, and next & prev pointers point to sf_free_list_heads[NUM_FREE_LISTS-1]

        void* additionalPage = sf_mem_grow();
        void* currentAddress = additionalPage;

        if (additionalPage == NULL){ // If there is no available memory left
            sf_errno = ENOMEM;
            return NULL;
        }
        else{
            // The heap begins with unused "padding"
            currentAddress += 24;

            // Set up first block of the heap, the prologue. This is an allocated block of minimum size (1M) w/ an unused payload area.
            // Set up header & footer. Address of footer: header address + block size - 8
            sf_block* prologue = currentAddress;
            prologue->header = (32 | THIS_BLOCK_ALLOCATED);
            sf_footer *prologueFooterAddress = getFooterAddress(prologue);
            *prologueFooterAddress = prologue->header;

            // Set up epilogue, which consists only of an allocated header, with block size set to 0.
            sf_block* epilogue = additionalPage + PAGE_SZ - 8;
            epilogue->header = (0 | THIS_BLOCK_ALLOCATED);

            // Set wilderness block header & footer
            sf_block* wildernessFreeBlock = additionalPage + 24 + getBlockSize(prologue);
            wildernessFreeBlock->header = PAGE_SZ - 24 - getBlockSize(prologue) - getBlockSize(epilogue);
            sf_footer *wildernessFooterAddress = getFooterAddress(wildernessFreeBlock);
            *wildernessFooterAddress = wildernessFreeBlock->header;

            // Set wilderness block next & prev pointers
            wildernessFreeBlock->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];
            wildernessFreeBlock->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];

            // Insert remainder memory as free block into wilderness freelist
            sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = wildernessFreeBlock;
            sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = wildernessFreeBlock;
        }
    }

    // Determine the size of the block to be allocated by adding the header size, footer size, and the size of any necessary padding
    //      to reach a size that is a multiple of 32 to maintain proper alignment.
    size_t calculatedSize = 8 + size + 8;
    size_t requiredBlockSize = 0;
    while (requiredBlockSize < calculatedSize){
        requiredBlockSize += 32;
    }

    // Determine the index of the free list that would be able to satisfy a request of specified size..
    int index = findFirstValidFreeList(requiredBlockSize);
    while (index < NUM_FREE_LISTS-1){
        if (!listIsEmpty(index)){
            // List is not empty. Now, search current free list from beginning until the first sufficiently large block is found.
            sf_block* firstValidBlock = getFirstFit(index, requiredBlockSize);

            // If a big enough block is found, then after splitting it (if it will not leave a splinter), insert the remainder part back
            //      into the appropriate freelist. Allocate the block & return pointer to header. Make sure to remove the current block from its free list.
            // Will reuse code from below. Should implement as its own function.
            if (firstValidBlock != NULL){
                if (splitWillSplinter(firstValidBlock, requiredBlockSize)){  // Allocate whole block
                    // Set the allocated bit of the block to 0
                   firstValidBlock->header = (getBlockSize(firstValidBlock) | THIS_BLOCK_ALLOCATED);

                    // Remove the free block from its free list
                    sf_free_list_heads[index].body.links.next = &sf_free_list_heads[index];
                    sf_free_list_heads[index].body.links.prev = &sf_free_list_heads[index];

                    // Return pointer to valid region of memory of requested size
                    return firstValidBlock->body.payload;
                }
                else{
                    // Split block
                    sf_block* remainderBlock = splitBlock(firstValidBlock, requiredBlockSize);

                    // Set pointers in the remainderBlock to the head of sentinel
                    remainderBlock->body.links.next = &sf_free_list_heads[index];
                    remainderBlock->body.links.prev = &sf_free_list_heads[index];

                    // Insert the remainder part back into the appropriate freelist
                    int newIndex = findFirstValidFreeList(getBlockSize(remainderBlock));
                    sf_free_list_heads[newIndex].body.links.next = remainderBlock;
                    sf_free_list_heads[newIndex].body.links.prev = remainderBlock;

                    // Return pointer to valid region of memory of requested size
                    return firstValidBlock->body.payload;
                }
            }

            // If there is no such block, continue w/ the next larger size class, until a nonempty list is found.
        }
        index++;
    }

    if (index == 7){ // Wilderness block must be used to satisfy request since the previous lists were all empty
        // if wilderness block is large enough to satisfy the request
        sf_block* wildernessFreeBlock = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next;
        int wildernessBlockSize = getBlockSize(wildernessFreeBlock);
        if (wildernessBlockSize >= requiredBlockSize){
            // Satisfy request depending on whether splitting it will create a splinter or not
            if (splitWillSplinter(wildernessFreeBlock, requiredBlockSize)){  // Allocate whole block
                // Set the allocated bit of the block to 0
                wildernessFreeBlock->header = (getBlockSize(wildernessFreeBlock) | THIS_BLOCK_ALLOCATED);

                // Remove the free block from the wilderness free list
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

                // Return pointer to valid region of memory of requested size
                return wildernessFreeBlock->body.payload;
            }
            else{
                // Split block
                sf_block* remainderBlock = splitBlock(wildernessFreeBlock, requiredBlockSize);

                // Set pointers in the remainderBlock. Since it will be the new wilderness free block, set its pointers to the head of sentinel
                remainderBlock->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
                remainderBlock->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

                // Insert the remainder part back into the appropriate freelist (back into the wilderness free list block)
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = remainderBlock;
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = remainderBlock;

                // printf("Address of block/header: %p\n", wildernessFreeBlock);
                // printf("Returned address (to payload): %p\n\n", (void*)wildernessFreeBlock + sizeof(sf_header));
                // Return pointer to valid region of memory of requested size
                // sf_show_block(wildernessFreeBlock);
                return wildernessFreeBlock->body.payload;
            }
        }
        else{
            // if the wilderness block is not already large enough to satisfy the request
            // call sf_mem_grow until either the allocator cannot satisfy the request, or if after coalescing the newly allocated page
            //      w/ the free block in the wilderness free list (if any), is large enough to satisfy request

            // while the wilderness block free list is empty or while the wilderness free block is not large enough
            while (sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1] ||
                requiredBlockSize > getBlockSize(sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next)){

                void* requestedPage = sf_mem_grow();

                // If allocator cannot satisfy the request
                if (requestedPage == NULL){
                    sf_errno = ENOMEM;
                    return NULL;
                }

                // Check if the wilderness free list is empty. If so, the newly allocated page should be the new wilderness block.
                if (sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next == &sf_free_list_heads[NUM_FREE_LISTS-1] &&
                    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev == &sf_free_list_heads[NUM_FREE_LISTS-1]){
                    sf_block* newWildernessBlock = requestedPage;

                    // Set header and footer
                    newWildernessBlock->header = PAGE_SZ;
                    sf_footer* newWildernessBlockFooter = getFooterAddress(newWildernessBlock);
                    *newWildernessBlockFooter = newWildernessBlock->header;

                    // Set links
                    newWildernessBlock->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
                    newWildernessBlock->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

                    // Set new epilogue
                    sf_block* epilogue = requestedPage - 8;
                    epilogue->header = (0 | THIS_BLOCK_ALLOCATED);
                }
                else{
                    // Coalesce the wilderness free block with new page
                    sf_block* newBlock = coalesceBlockWithPage(sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next);

                    // Old epilogue as header of new block
                    sf_block* oldEpilogue = (void*)sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next + getBlockSize(sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next);
                    oldEpilogue->header = getBlockSize(newBlock);

                    // Set footer
                    sf_footer* footer = getFooterAddress(newBlock);
                    *footer = getBlockSize(newBlock);

                    // Create new epilogue at the end of the newly added region
                    sf_block* newEpilogue = (void*)newBlock + getBlockSize(newBlock);
                    newEpilogue->header = (0 | THIS_BLOCK_ALLOCATED);

                    // Insert the new block at the beginning of the appropriate freelist (always the wilderness block)
                    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = newBlock;
                    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = newBlock;
                }
            }

            // Satisfy the request now that the wilderness block is large enough
            // MAKE MODULAR. THE IF AND ELSE STATEMENT BELOW IS THE SAME AS ABOVE (IF WILDERNESS BLOCK WAS LARGE ENOUGH W/O CALLING SF_MEM_GROW)
            if (splitWillSplinter(wildernessFreeBlock, requiredBlockSize)){  // Allocate whole block
                // Set the allocated bit of the block to 0
                wildernessFreeBlock->header = (getBlockSize(wildernessFreeBlock) | THIS_BLOCK_ALLOCATED);

                // Remove the free block from the wilderness free list
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

                // Return pointer to valid region of memory of requested size
                return wildernessFreeBlock->body.payload;
            }
            else{
                // Split block
                sf_block* remainderBlock = splitBlock(wildernessFreeBlock, requiredBlockSize);

                // Set pointers in the remainderBlock. Since it will be the new wilderness free block, set its pointers to the head of sentinel
                remainderBlock->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];
                remainderBlock->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];

                // Insert the remainder part back into the appropriate freelist (back into the wilderness free list block)
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = remainderBlock;
                sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = remainderBlock;

                // Return pointer to valid region of memory of requested size
                return wildernessFreeBlock->body.payload;
            }
        }
    }

    return NULL;
}

/*
 * Marks a dynamically allocated region as no longer in use.
 * Adds the newly freed block to the free list.
 *
 * @param ptr Address of memory returned by the function sf_malloc.
 *
 * If ptr is invalid, the function calls abort() to exit the program.
 */

void sf_free(void *pp) {
    if (!pointerIsValid(pp)) abort();
    sf_block* block = (sf_block*)(pp - sizeof(sf_header));

    // Pointer given is valid, so free the block.
    // Get pointers to adjacent blocks
    sf_footer* prevBlockFooter = (void*)block - 8;
    int mask = 0xFFFFFFFF;
    mask = mask << 5;
    size_t prevBlockSize = *prevBlockFooter & mask;
    sf_block* prevBlock = (void*)block - prevBlockSize;
    sf_block* nextBlock = (void*)block + getBlockSize(block);

    int coalescedBlockIsWilderness = 0;

    // If the adjacent blocks are in the heap, attempt to coalesce. If coalesced, remove the block from its free list.
    if ((void*)prevBlock >= sf_mem_start() && (void*)prevBlock <= sf_mem_end()){
        if (blockIsFree(prevBlock)){
            if (isWildernessBlock(prevBlock)){
                coalescedBlockIsWilderness = 1;
                removeFromItsList(prevBlock, NUM_FREE_LISTS-1);
            }
            else{
                removeFromItsList(prevBlock, findFirstValidFreeList(getBlockSize(prevBlock)));
            }
            block = coalesceBlockWithBlock(block, prevBlock);
        }
    }

    if ((void*)nextBlock >= sf_mem_start() && (void*)nextBlock <= sf_mem_end()){
        if (blockIsFree(nextBlock)){
            if (isWildernessBlock(nextBlock)){
                coalescedBlockIsWilderness = 1;
                removeFromItsList(nextBlock, NUM_FREE_LISTS-1);
            }
            else{
                removeFromItsList(nextBlock, findFirstValidFreeList(getBlockSize(nextBlock)));
            }
            block = coalesceBlockWithBlock(block, nextBlock);
        }
    }

    // Then insert the block at the front of the appropriate free list, after coalescing w/ any adjacent free block
    int appropriateFreeListIndex;
    if (coalescedBlockIsWilderness){
        appropriateFreeListIndex = NUM_FREE_LISTS-1;
    }
    else{
        appropriateFreeListIndex = findFirstValidFreeList(getBlockSize(block));
    }

    insertIntoList(block, appropriateFreeListIndex);

    // Blocks in a free list must not be marked as allocated (so change the allocation bit) and must have a valid footer w/ contents identical to header
    block->header = getBlockSize(block);
    sf_footer* footer = getFooterAddress(block);
    *footer = block->header;
}


/*
 * Resizes the memory pointed to by ptr to size bytes.
 *
 * @param ptr Address of the memory region to resize.
 * @param size The minimum size to resize the memory to.
 *
 * @return If successful, the pointer to a valid region of memory is
 * returned, else NULL is returned and sf_errno is set appropriately.
 *
 *   If sf_realloc is called with an invalid pointer sf_errno should be set to EINVAL.
 *   If there is no memory available sf_realloc should set sf_errno to ENOMEM.
 *
 * If sf_realloc is called with a valid pointer and a size of 0 it should free
 * the allocated block and return NULL without setting sf_errno.
 */

void *sf_realloc(void *pp, size_t rsize) {
    if (!pointerIsValid(pp)){
        sf_errno = EINVAL;
        return NULL;
    }
    if (false /* TODO */){     // If there is no memory available sf_realloc should set sf_errno to ENOMEM
        sf_errno = ENOMEM;
        return NULL;
    }
    if (rsize == 0){
        sf_free(pp);
        return NULL;
    }

    size_t totalSize = rsize + sizeof(sf_header) + sizeof(sf_footer);
    size_t requiredBlockSize = 0;
    while (requiredBlockSize < totalSize){
        requiredBlockSize += 32;
    }

    sf_block* block = (sf_block*)(pp - sizeof(sf_header));
    // Return pointer to a valid region of memory
    if (getBlockSize(block) == requiredBlockSize) return pp;

    // If reallocating to a larger size.
    if (getBlockSize(block) < requiredBlockSize){
        void* largerBlock = sf_malloc(rsize);
        if (largerBlock == NULL) return NULL;
        memcpy(largerBlock, pp, rsize);
        sf_free(pp);
        return largerBlock;
    }

    // Reallocating to a smaller size
    // Case with splinter: do nothing
    // Case with splinter:

    if (!splitWillSplinter(block, totalSize)){
        // Split the block and update the block size fields in both headers
        sf_block* remainderBlock = splitBlock(block, requiredBlockSize);

        // Free remainder block
        // Copied code from sf_free function, since that function cant be reused because it can only be used on pointers that were
        //      returned by malloc;
        sf_block* block = remainderBlock;

        // Pointer given is valid, so free the block.
        // Get pointers to adjacent blocks
        sf_footer* prevBlockFooter = (void*)block - 8;
        int mask = 0xFFFFFFFF;
        mask = mask << 5;
        size_t prevBlockSize = *prevBlockFooter & mask;
        sf_block* prevBlock = (void*)block - prevBlockSize;
        sf_block* nextBlock = (void*)block + getBlockSize(block);

        int coalescedBlockIsWilderness = 0;

        // If the adjacent blocks are in the heap, attempt to coalesce. If coalesced, remove the block from its free list.
        if ((void*)prevBlock >= sf_mem_start() && (void*)prevBlock <= sf_mem_end()){
            if (blockIsFree(prevBlock)){
                if (isWildernessBlock(prevBlock)){
                    coalescedBlockIsWilderness = 1;
                    removeFromItsList(prevBlock, NUM_FREE_LISTS-1);
                }
                else{
                    removeFromItsList(prevBlock, findFirstValidFreeList(getBlockSize(prevBlock)));
                }
                block = coalesceBlockWithBlock(block, prevBlock);
            }
        }

        if ((void*)nextBlock >= sf_mem_start() && (void*)nextBlock <= sf_mem_end()){
            if (blockIsFree(nextBlock)){
                if (isWildernessBlock(nextBlock)){
                    coalescedBlockIsWilderness = 1;
                    removeFromItsList(nextBlock, NUM_FREE_LISTS-1);
                }
                else{
                    removeFromItsList(nextBlock, findFirstValidFreeList(getBlockSize(nextBlock)));
                }
                block = coalesceBlockWithBlock(block, nextBlock);
            }
        }

        // Then insert the block at the front of the appropriate free list, after coalescing w/ any adjacent free block
        int appropriateFreeListIndex;
        if (coalescedBlockIsWilderness){
            appropriateFreeListIndex = NUM_FREE_LISTS-1;
        }
        else{
            appropriateFreeListIndex = findFirstValidFreeList(getBlockSize(block));
        }

        insertIntoList(block, appropriateFreeListIndex);

        // Blocks in a free list must not be marked as allocated (so change the allocation bit) and must have a valid footer w/ contents identical to header
        block->header = getBlockSize(block);
        sf_footer* footer = getFooterAddress(block);
        *footer = block->header;
        }

    return pp;
}

/*
 * Allocates a block of memory with a specified alignment.
 *
 * @param align The alignment required of the returned pointer.
 * @param size The number of bytes requested to be allocated.
 *
 * @return If align is not a power of two or is less than the minimum block size,
 * then NULL is returned and sf_errno is set to EINVAL.
 * If size is 0, then NULL is returned without setting sf_errno.
 * Otherwise, if the allocation is successful a pointer to a valid region of memory
 * of the requested size and with the requested alignment is returned.
 * If the allocation is not successful, then NULL is returned and sf_errno is set
 * to ENOMEM.
 */

void *sf_memalign(size_t size, size_t align) {
    return NULL;
}
