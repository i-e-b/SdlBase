#include "Vector.h"
#include "MemoryManager.h"

#include "RawData.h"

#include <cstdint>

typedef struct Vector {
    bool IsValid; // if this is false, creation failed

    Arena* _arena; // the arena this vector should be pinned to (the active one when the vector was created)

    // Calculated parts

    // Number of data entries in each chunk (should always be a power of 2)
    uint32_t ElemsPerChunk;
    // Log2 of `ElemsPerChunk`
    uint32_t ElemChunkLog2;
    // Number of bytes in each element
    uint32_t ElementByteSize;
    // Size of an allocated chunk (should be `ElemsPerChunk` * `ElementByteSize`)
    uint16_t ChunkBytes;

    // dynamic parts

    uint32_t _elementCount;     // how long is the logical array
    uint32_t _baseOffset;       // how many elements should be ignored from first chunk (for de-queueing)
    uint32_t  _skipEntries;              // how long is the logical skip table
    bool _skipTableDirty;           // does the skip table need updating?
    bool _rebuilding;               // are we in the middle of rebuilding the skip table?

    // Pointers to data
    // Start of the chunk chain
    char* _baseChunkTable;

    // End of the chunk chain
    char* _endChunkPtr;

    // Pointer to skip table
    void* _skipTable;
} Vector;


#ifndef NULL
#define NULL 0
#endif
#ifndef uint
#define uint uint32_t
#endif
#ifndef var
#define var auto
#endif

// Fixed sizes -- these are structural to the code and must not change
const int PTR_SIZE = sizeof(void*); // system pointer equivalent
const int INDEX_SIZE = sizeof(unsigned int); // size of index data
const int SKIP_ELEM_SIZE = INDEX_SIZE + PTR_SIZE; // size of skip list entries

// Tuning parameters: have a play if you have performance or memory issues.
const int ARENA_SIZE = 65535; // number of bytes for each chunk (limit -- should match any restriction in the allocator)

// Desired maximum elements per chunk. This will be reduced if element is large (to fit in Arena limit)
// Larger values are significantly faster for big arrays, but more memory-wasteful on small arrays
// This should ALWAYS be a power-of-2
const int TARGET_ELEMS_PER_CHUNK = 128;

// Maximum size of the skip table.
// This is dynamically sizes, so large values won't use extra memory for small arrays.
// This limits the memory growth of larger arrays. If it's bigger than an arena, everything will fail.
const long SKIP_TABLE_SIZE_LIMIT = 2048;

/*
 * Structure of the element chunk:
 *
 * [Ptr to next chunk, or -1]    <- sizeof(void*)
 * [Chunk value (if set)]        <- sizeof(Element)
 * . . .
 * [Chunk value]                 <- ... up to ChunkBytes
 *
 */

 /*
  * Structure of skip table
  *
  * [ChunkIdx]      <-- 4 bytes (uint)
  * [ChunkPtr]      <-- 8 bytes (ptr)
  * . . .
  * [ChunkIdx]
  * [ChunkPtr]
  *
  * Recalculate that after a preallocate, or after a certain number of chunks
  * have been added. This table could be biased, but for simplicity just
  * evenly distribute for now.
  */

void MaybeRebuildSkipTable(Vector *v); // defined later

// abstract over alloc/free to help us pin to one arena
inline void* VecCAlloc(Vector *v, int count, size_t size) {
    if (v->_arena == nullptr) return nullptr;
    return ArenaAllocateAndClear(v->_arena, count*size);
}
inline void VecFree(Vector *v, void* ptr) {
    ArenaDereference(v->_arena, ptr);
}
inline void* VecAlloc(Vector *v, size_t size) {
    if (v->_arena == nullptr) return nullptr;
    return ArenaAllocate(v->_arena,size);
}

// add a new chunk at the end of the chain
void *NewChunk(Vector *v) {
    auto ptr = VecCAlloc(v, 1, v->ChunkBytes); // calloc to avoid garbage data in the chunks
    if (ptr == nullptr) return nullptr;

    ((size_t*)ptr)[0] = 0; // set the continuation pointer of the new chunk to invalid
    if (v->_endChunkPtr != nullptr) ((size_t*)v->_endChunkPtr)[0] = (size_t)ptr;  // update the continuation pointer of the old end chunk
    v->_endChunkPtr = (char*)ptr; // update the end chunk pointer

    return ptr;
}

// It's very important this is correct and optimal
// `chunkPtr` returns the memory address of a vector chunk
// `chunkIndex` returns the chunk index where the logical index was found
bool FindNearestChunk(Vector *v, uint32_t targetIndex, void **chunkPtr, unsigned int *chunkIndex) {
	uint baseOffset = v->_baseOffset;
	uint elemCount = v->_elementCount;
	uint realElemCount = elemCount + baseOffset;

    if (realElemCount < v->ElemsPerChunk) { // Optimised for near-empty list
        *chunkPtr = v->_endChunkPtr;
        *chunkIndex = 0;
        return true;
    }

    // 0. Apply offsets
    uint realIndex = targetIndex + baseOffset;

    // 1. Calculate desired chunk index
    uint targetChunkIdx = realIndex >> v->ElemChunkLog2;
	*chunkIndex = targetChunkIdx;

    // 2. Optimise for start- and end- of chain (small lists & very likely for Push & Pop)

    if (targetChunkIdx == 0) { // start of chain
        *chunkPtr = v->_baseChunkTable;
        return true;
    }

    uint endChunkIdx = (realElemCount - 1) >> v->ElemChunkLog2;
    if (targetChunkIdx == endChunkIdx)
    { // lands on end-of-chain
        *chunkPtr = v->_endChunkPtr;
        return true;
    }
    if (targetIndex >= elemCount)
    { // lands outside a chunk -- off the end
        *chunkPtr = v->_endChunkPtr;
        return false;
    }

    // All the simple optimal paths failed. Make sure the skip list is good...
    MaybeRebuildSkipTable(v);

    // 3. Use the skip table to find a chunk near the target
    //    By ensuring the skip table is fresh, we can calculate the correct location
    uint startChunkIdx = 0;
    void* chunkHeadPtr = v->_baseChunkTable;

    if (v->_skipEntries > 1)
    {
        // guess search bounds
        auto guess = ((targetChunkIdx - 1) * v->_skipEntries) / endChunkIdx;
        auto lower = guess - 1;
        if (lower < 0) lower = 0;

        var baseAddr = byteOffset(v->_skipTable, (SKIP_ELEM_SIZE * lower)); // pointer to skip table entry
        startChunkIdx = readUint(baseAddr, 0);
        chunkHeadPtr = readPtr(baseAddr, INDEX_SIZE);
    }

    var walk = targetChunkIdx - startChunkIdx;
    if (walk > 4 && v->_skipEntries < SKIP_TABLE_SIZE_LIMIT) {
        v->_skipTableDirty = true; // if we are walking too far, try building a better table
    }

    // 4. Walk the chain until we find the chunk we want
    for (; startChunkIdx < targetChunkIdx; startChunkIdx++)
    {
        auto next = readPtr(chunkHeadPtr, 0);
        if (next == nullptr) {
            // walk chain failed! We will store the last step in case it's useful.
            *chunkPtr = chunkHeadPtr;
            return false;
        }
        chunkHeadPtr = next;
    }

    *chunkPtr = chunkHeadPtr;
	return true;
}

inline void RebuildSkipTable(Vector *v)
{
    v->_rebuilding = true;
    v->_skipTableDirty = false;
    auto chunkTotal = v->_elementCount >> v->ElemChunkLog2;
    if (chunkTotal < 4) { // not worth having a skip table
        if (v->_skipTable != nullptr) VecFree(v, v->_skipTable);
        v->_skipEntries = 0;
        v->_skipTable = nullptr;
        v->_rebuilding = false;
        return;
    }

    // Guess a reasonable size for the skip table
    auto entries = (chunkTotal < SKIP_TABLE_SIZE_LIMIT) ? chunkTotal : SKIP_TABLE_SIZE_LIMIT;

    // General case: not every chunk will fit in the skip table
    // Find representative chunks using the existing table.
    // (finding will be a combination of search and scan)
    auto newTablePtr = VecAlloc(v, SKIP_ELEM_SIZE * entries);
    if (newTablePtr == nullptr) { v->_rebuilding = false; return; } // live with the old one

    auto stride = v->_elementCount / entries;
    if (stride < 1) stride = 1;

    auto target = 0ul;
    auto newSkipEntries = 0;
    bool found;
    void *chunkPtr;
    unsigned int chunkIndex;

    for (uint i = 0; i < entries; i++) {
        found = FindNearestChunk(v, target, &chunkPtr, &chunkIndex);

        if (!found || chunkPtr == nullptr) { // total fail
            VecFree(v,newTablePtr);
            v->_rebuilding = false;
            return;
        }

        var indexPtr = byteOffset(newTablePtr, (SKIP_ELEM_SIZE * i)); // 443
        writeUint(indexPtr, 0, chunkIndex);
        writePtr(indexPtr, INDEX_SIZE, chunkPtr); // TODO: !!! This fails at large sizes!
        newSkipEntries++;
        target += stride;
    }

    if (newSkipEntries < 1) {
        VecFree(v, newTablePtr); // failed to build
        v->_rebuilding = false;
        return;
    }

    v->_skipEntries = newSkipEntries;
    if (v->_skipTable != nullptr) VecFree(v, v->_skipTable);
    v->_skipTable = newTablePtr;
    v->_rebuilding = false;
}

inline void MaybeRebuildSkipTable(Vector *v) {
    if (v->_rebuilding) return;

    // If we've added a few chunks since last update, then refresh the skip table
    if (v->_skipTableDirty) RebuildSkipTable(v);
}

// Find the base pointer of the data in the given vector slot.
// This gets *hammered* by many calls in the system, so it's a good idea to keep it optimal.
inline void * PtrOfElem(Vector *v, uint32_t index) {
    if (v == nullptr) return nullptr;
    while (index < 0) { index += (int)(v->_elementCount); } // allow negative index syntax
    if (index >= v->_elementCount) return nullptr;

    var entryIdx = (index + v->_baseOffset) % v->ElemsPerChunk;

	/* */
	uint baseOffset = v->_baseOffset;
	uint elemCount = v->_elementCount;
	uint realElemCount = elemCount + baseOffset;

    if (realElemCount < v->ElemsPerChunk) { // Optimised for near-empty list
		return byteOffset(v->_endChunkPtr, PTR_SIZE + (v->ElementByteSize * entryIdx));
    }
	/* */

    void *chunkPtr = nullptr;
    uint32_t realIdx = 0;
    if (!FindNearestChunk(v, index, &chunkPtr, &realIdx)) return nullptr;

    return byteOffset(chunkPtr, PTR_SIZE + (v->ElementByteSize * entryIdx));
}


uint32_t Log2(uint32_t i) {
    uint32_t r = 0;

    while (i > 0) {
        r++;
        i >>= 1;
    }
    return r - 1;
}

// Create a new dynamic vector with the given element size (must be fixed per vector) in a specific memory arena
Vector *VectorAllocateArena(Arena* a, size_t elementSize) {
    if (a == nullptr) return nullptr;
    auto result = (Vector*)ArenaAllocateAndClear(a, sizeof(Vector));
    if (result == nullptr) return nullptr;

    result->_arena = a;
    result->ElementByteSize = elementSize;

    // Work out how many elements can fit in an arena
    auto spaceForElements = ARENA_SIZE - PTR_SIZE; // need pointer space
    result->ElemsPerChunk = (int)(spaceForElements / result->ElementByteSize);

    if (result->ElemsPerChunk <= 1) {
        result->IsValid = false;
        return result;
    }

    if (result->ElemsPerChunk > TARGET_ELEMS_PER_CHUNK)
        result->ElemsPerChunk = TARGET_ELEMS_PER_CHUNK; // no need to go crazy with small items.

    // Force to a power-of-two, and store the log2 of that (to use as a bit-shift parameter)
    result->ElemsPerChunk = NextPow2(result->ElemsPerChunk - 1); // force to a power of two
    result->ElemChunkLog2 = Log2(result->ElemsPerChunk);

    result->ChunkBytes = (unsigned short)(PTR_SIZE + (result->ElemsPerChunk * result->ElementByteSize));

    // Make a table, which can store a few chunks, and can have a next-chunk-table pointer
    // Each chunk can hold a few elements.
    result->_skipEntries = 0;
    result->_skipTable = nullptr;
    result->_endChunkPtr = nullptr;
    result->_baseChunkTable = nullptr;

    auto baseTable = NewChunk(result);

    if (baseTable == nullptr) {
        result->IsValid = false;
        return result;
    }
    result->_baseChunkTable = (char*)baseTable;
    result->_elementCount = 0;
    result->_baseOffset = 0;
    RebuildSkipTable(result);

    // All done
    result->IsValid = true;
    return result;
}

Vector *VectorAllocate(size_t elementSize) {
    return VectorAllocateArena(MMCurrent(), elementSize);
}

bool VectorIsValid(Vector *v) {
    if (v == nullptr) return false;
    return v->IsValid;
}

void VectorClear(Vector *v) {
    if (v == nullptr) return;
    if (!v->IsValid) return;

    v->_elementCount = 0;
    v->_baseOffset = 0;
    v->_skipEntries = 0;

    // empty out the skip table, if present
    if (v->_skipTable != nullptr) {
        VecFree(v, v->_skipTable);
        v->_skipTable = nullptr;
    }

    // Walk through the chunk chain, removing until we hit an invalid pointer
    var current = readPtr(v->_baseChunkTable, 0); // read from *second* chunk, if present
    v->_endChunkPtr = v->_baseChunkTable;
    writePtr(v->_baseChunkTable, 0, nullptr); // write a null into the chain link

    while (current != nullptr) {
        var next = readPtr(current, 0);
        writePtr(current, 0, nullptr); // just in case we have a loop
        VecFree(v, current);
        current = next;
    }
}

void VectorDeallocate(Vector *v) {
    if (v == nullptr) return;
    v->IsValid = false;
    if (v->_skipTable != nullptr) VecFree(v, v->_skipTable);
    v->_skipTable = nullptr;
    // Walk through the chunk chain, removing until we hit an invalid pointer
    var current = v->_baseChunkTable;
    while (true) {
        if (current == nullptr) break; // end of chunks

        var next = readPtr(current, 0);
        writePtr(current, 0, nullptr); // just in case we have a loop
        VecFree(v, current);

        if (current == v->_endChunkPtr) break; // sentinel
        current = (char*)next;
    }
    v->_baseChunkTable = nullptr;
    v->_endChunkPtr = nullptr;
    v->_elementCount = 0;
    v->ElementByteSize = 0;
    v->ElemsPerChunk = 0;

    auto a = v->_arena;
	ArenaDereference(a, v);
}

unsigned int VectorLength(Vector *v) {
    if (v == nullptr) return 0;
    return v->_elementCount;
}

bool VectorPush(Vector *v, void* value) {
    if (v == nullptr) return false;
    var entryIdx = (v->_elementCount + v->_baseOffset) % v->ElemsPerChunk;

    void *chunkPtr = nullptr;
    uint index;
    if (!FindNearestChunk(v, v->_elementCount, &chunkPtr, &index)) // need a new chunk, write at start
    {
        var ok = NewChunk(v);
        if (ok == nullptr) {
            v->IsValid = false;
            return false;
        }
        writeValue(v->_endChunkPtr, PTR_SIZE, value, v->ElementByteSize);
        v->_elementCount++;
        return true;
    }
    if (chunkPtr == nullptr) return false;

    // Writing value into existing chunk
    writeValue(chunkPtr, PTR_SIZE + (v->ElementByteSize * entryIdx), value, v->ElementByteSize);
    v->_elementCount++;

    return true;
}

void* VectorGet(Vector *v, int index) {
    return PtrOfElem(v, index);
}

// Free cache memory
void VectorFreeCache(Vector* v, void* cache) {
    VecFree(v, cache);
}

void* VectorCacheRange(Vector* v, uint32_t* lowIndex, uint32_t* highIndex) {
    if (v == nullptr || v->_elementCount < 1) return nullptr;
    uint32_t chunkIndex = 0;
    void* chunkPtr = nullptr;

    // force to range, and update
    if ((*lowIndex) < 0) *lowIndex = 0;
    if ((*highIndex) >= v->_elementCount) *highIndex = v->_elementCount - 1;

    auto requiredElems = ((*highIndex) - (*lowIndex)) + 1;

    // get offsets for optimisations
    auto maxIdx = v->ElemsPerChunk;
    uint32_t index = ((*lowIndex) + v->_baseOffset) % v->ElemsPerChunk;
   
    // find first chunk
    if (!FindNearestChunk(v, *lowIndex, &chunkPtr, &chunkIndex)) return nullptr;

    // make memory for our chunk
    auto esz = v->ElementByteSize;
    auto block = VecAlloc(v, esz * requiredElems);
    if (block == nullptr) return nullptr;

    // now, scan along (switching chunk as required) until full
    auto dst = block;
    auto src = byteOffset(chunkPtr, PTR_SIZE + (esz * index));
    while (requiredElems > 0) {
        writeValue(dst, 0, src, esz);
        dst = byteOffset(dst, esz);

        requiredElems--;
        index++;
        if (index >= maxIdx) { // move to next chunk
            index = 0;
            chunkPtr = readPtr(chunkPtr, 0);
            src = byteOffset(chunkPtr, PTR_SIZE);
        } else { // next element in same chunk
            src = byteOffset(src, esz);
        }
    }

    return block;
}

bool VectorCopy(Vector * v, unsigned int index, void * outValue)
{
    var ptr = PtrOfElem(v, index);
    if (ptr == nullptr) return false;

    writeValue(outValue, 0, ptr, v->ElementByteSize);
    return true;
}

bool VectorDequeue(Vector * v, void * outValue) {
    // Special cases:
    if (v == nullptr) return false;
    if (!v->IsValid) return false;
    if (v->_elementCount < 1) return false;

    // read the element at index `_baseOffset`, then increment `_baseOffset`.
    if (outValue != nullptr) {
        auto ptr = byteOffset(v->_baseChunkTable, PTR_SIZE + (v->_baseOffset * v->ElementByteSize));
        writeValue(outValue, 0, ptr, v->ElementByteSize);
    }
    v->_baseOffset++;
    v->_elementCount--;

    // If there's no clean-up to be done, jump out now.
    if (v->_baseOffset < v->ElemsPerChunk) return true;

    // If `_baseOffset` is equal to chunk length, deallocate the first chunk.
    // When we we deallocate a chunk, copy a truncated version of the skip table
    // if we're on the last chunk, don't deallocate, but just reset the base offset.

    v->_baseOffset = 0;
    auto nextChunk = readPtr(v->_baseChunkTable, 0);
    if (nextChunk == nullptr || v->_baseChunkTable == v->_endChunkPtr) { // this is the only chunk
        return true;
    }
    // Advance the base and free the old
    auto oldChunk = v->_baseChunkTable;
    v->_baseChunkTable = (char*)nextChunk;
    VecFree(v, oldChunk);

    if (v->_skipTable == nullptr) return true; // don't need to fix the table

    // Make a truncated version of the skip table
    v->_skipEntries--;
    if (v->_skipEntries < 4) { // no point having a table
        VecFree(v, v->_skipTable);
        v->_skipEntries = 0;
        v->_skipTable = nullptr;
        return true;
    }
    // Copy of the skip table with first element gone
    uint length = SKIP_ELEM_SIZE * v->_skipEntries;
    auto newTablePtr = (char*)VecAlloc(v, length);
    auto oldTablePtr = (char*)v->_skipTable;
    for (uint i = 0; i < length; i++) {
        newTablePtr[i] = oldTablePtr[i + SKIP_ELEM_SIZE];
    }
    v->_skipTable = newTablePtr;
    VecFree(v, oldTablePtr);

    return true;
}

bool VectorPop(Vector *v, void *target) {
    if (v == nullptr || v->_elementCount == 0) return false;

    var index = v->_elementCount - 1;
    var entryIdx = (index + v->_baseOffset) % v->ElemsPerChunk;

    // Get the value
    var result = byteOffset(v->_endChunkPtr, PTR_SIZE + (v->ElementByteSize * entryIdx));
    if (result == nullptr) return false;
    if (target != nullptr) { // need to copy element, as we might dealloc the chunk it lives in
        writeValue(target, 0, result, v->ElementByteSize);
    }

    // Clean up if we've emptied a chunk that isn't the initial one
    if (entryIdx < 1 && v->_elementCount > 1) {
        // need to dealloc end chunk
        void *prevChunkPtr = nullptr;
        uint deadChunkIdx = 0;
        if (!FindNearestChunk(v, index - 1, &prevChunkPtr, &deadChunkIdx)
            || prevChunkPtr == v->_endChunkPtr) {
            // damaged references!
            v->IsValid = false;
            return false;
        }
        VecFree(v, v->_endChunkPtr);
        v->_endChunkPtr = (char*)prevChunkPtr;
        writePtr(prevChunkPtr, 0, nullptr); // remove the 'next' pointer from the new end chunk

        if (v->_skipEntries > 0) {
            // Check to see if we've made the skip list invalid
            var skipTableEnd = readUint(v->_skipTable, SKIP_ELEM_SIZE * (v->_skipEntries - 1));

            // knock the last element off if it's too big. 
            // The walk limit in FindNearestChunk set the dirty flag if needed
            if (skipTableEnd >= deadChunkIdx) {
                v->_skipEntries--;
            }
        }
    }

    v->_elementCount--;
    return true;
}

bool VectorPeek(Vector *v, void* target) {
    if (v->_elementCount == 0) return false;

    var index = v->_elementCount - 1;
    var entryIdx = (index + v->_baseOffset) % v->ElemsPerChunk;

    // Get the value
    var result = byteOffset(v->_endChunkPtr, PTR_SIZE + (v->ElementByteSize * entryIdx));
    if (result == nullptr) return false;
    if (target != nullptr) {
        writeValue(target, 0, result, v->ElementByteSize);
    }
    return true;
}

bool VectorSet(Vector *v, int index, void* element, void* prevValue) {
    // push in the value, returning previous value
    var ptr = PtrOfElem(v, index);
    if (ptr == nullptr) return false;

    if (prevValue != nullptr) {
        writeValue(prevValue, 0, ptr, v->ElementByteSize);
    }

    writeValue(ptr, 0, element, v->ElementByteSize);
    return true;
}

bool VectorPreallocate(Vector *v, unsigned int length) {
    var remain = length - v->_elementCount;
    if (remain < 1) return true;

    var newChunkIdx = length / v->ElemsPerChunk;

    // Walk through the chunk chain, adding where needed
    var chunkHeadPtr = v->_baseChunkTable;
    for (uint32_t i = 0; i < newChunkIdx; i++)
    {
        var nextChunkPtr = readPtr(chunkHeadPtr, 0);
        if (nextChunkPtr == nullptr) {
            // need to alloc a new chunk
            nextChunkPtr = NewChunk(v);
            if (nextChunkPtr == nullptr) return false;
        }
        chunkHeadPtr = (char*)nextChunkPtr;
    }

    v->_elementCount = length;

    RebuildSkipTable(v); // make sure we're up to date

    return true;
}


inline bool VectorSwapInternal(Vector *v, unsigned int index1, unsigned int index2, void* tmp, int bytes) {
    var A = PtrOfElem(v, index1);
    var B = PtrOfElem(v, index2);

    if (A == nullptr || B == nullptr) return false;

    writeValue(tmp, 0, A, bytes); // tmp = A
    writeValue(A, 0, B, bytes); // A = B
    writeValue(B, 0, tmp, bytes); // B = tmp

    return true;
}

bool VectorSwap(Vector *v, unsigned int index1, unsigned int index2) {
    if (v == nullptr) return false;

    var bytes = v->ElementByteSize;
    var tmp = VecAlloc(v, bytes);
    if (tmp == nullptr) return false;

    VectorSwapInternal(v, index1, index2, tmp, (int)bytes);

    VecFree(v, tmp);

    return true;
}

bool VectorReverse(Vector *v) {
    // we could probably optimise this quite a lot
    if (v == nullptr) return false;

    auto bytes = v->ElementByteSize;
    auto tmp = VecAlloc(v, bytes);
    if (tmp == nullptr) return false;

    auto end = v->_elementCount;
    auto halfLength = end / 2;
    for (uint32_t i = 0; i < halfLength; i++) {
        end--;
        VectorSwapInternal(v, i, end, tmp, (int)bytes);
    }

    VecFree(v, tmp);

    return true;
}

// Merge with minimal copies. Input values should be in arr1. Returns the array that the final result is in.
// The input array should have had the first set of swaps done (partition size = 1)
// Compare should return 0 if the two values are equal, negative if A should be before B, and positive if B should be before A.
void* IterativeMergeSort(void* arr1, void* arr2, int n, int elemSize, int(*compareFunc)(void*, void*)) {
    auto A = arr2; // we will be flipping the array pointers around
    auto B = arr1;

    for (int stride = 2; stride < n; stride *= 2) { // doubling merge width

        // swap A and B pointers after each merge set
        { auto tmp = A; A = B; B = tmp; }

        int t = 0; // incrementing point in target array
        for (int left = 0; left < n; left += 2 * stride) {
            int right = left + stride;
            int end = right + stride;
            if (end > n) end = n; // some merge windows will run off the end of the data array
            if (right > n) right = n; // some merge windows will run off the end of the data array
            int l = left, r = right; // the point we are scanning though the two sets to be merged.

            // copy the lowest candidate across from A to B
            while (l < right && r < end) {
                int lower = (compareFunc(byteOffset(A, l * elemSize), byteOffset(A, r * elemSize)) < 0)
                          ? (l++) : (r++);
                copyAnonArray(B, t++, A, lower, elemSize); // B[t++] = A[lower];
            } // exhausted at least one of the merge sides

            while (l < right) { // run down left if anything remains
                copyAnonArray(B, t++, A, l++, elemSize); // B[t++] = A[l++];
            }

            while (r < end) { // run down right side if anything remains
                copyAnonArray(B, t++, A, r++, elemSize); // B[t++] = A[r++];
            }
        }
    }

    // let the caller know which array has the result
    return B;
}

void VectorSort(Vector *v, int(*compareFunc)(void*, void*)) {
    // Available Plans:

    // A - normal merge: (extra space ~N + chunkSize)
    // 1. in each chunk, run the simple array-based merge-sort
    // 2. merge-sort between pairs of chunks
    // 3. keep merging pairs into longer chains until all sorted
    //
    // B - tide-front (insertion sort) (extra space ~N)
    // 1. in each chunk, run the simple array-based sort
    // 2. build a 'tide' array, one element per chunk
    // 3. pick the lowest element in the array, and replace with next element from that chunk
    // 4. repeat until all empty
    //
    // C - compare-exchange (extra space ~chunkSize)
    // 1. each chunk merged
    // 2. we pick a chunk and merge it in turn with each other chunk (so one has values always smaller than other)
    // 3. repeat until sorted (see notebook)
    //
    // D - empty and re-fill (extra space ~2N) -- the optimised nature of push/pop might make this quite good.
    // 1. make 2 arrays big enough for the vector
    // 2. empty the vector into one of them (can do this as first part of sort [2-pairs])
    // 3. sort normally
    // 4. push back into vector
    //
    // E - proxy sort (extra space ~N)
    // 1. Change the compare function to a 'make-ordinal'
    // 2. Write a proxy array of current position and ordinal
    // 3. Sort that
    // 4. swap elements in the vector to match

    // Going to start with 'D' as a base-line

    // Allocate the temporary structures
    uint32_t n = VectorLength(v);
    auto size = v->ElementByteSize;
    void* arr1 = VecAlloc(v, (n+1) * size); // extra space for swapping
    if (arr1 == nullptr) return;
    void* arr2 = VecAlloc(v, n * size);
    if (arr2 == nullptr) { VecFree(v, arr1); return; }

    // write into array in pairs, doing the scale=1 merge
    uint32_t i = 0;
    while (true) {
        void* a = byteOffset(arr1, i * size);
        void* b = byteOffset(arr1, (i+1) * size);
        bool _a = VectorDequeue(v, a);
        bool _b = VectorDequeue(v, b);

        if (_a && _b) { // wrote 2: compare and swap if required
            if (compareFunc(a, b) > 0) swapMem(a, b, size);
        } else { // empty vector
            break;
        }
        i += 2;
    }

    // do the sort
    auto result = IterativeMergeSort(arr1, arr2, (int)n, (int)size, compareFunc);

    // push the result back into the vector
    for (i = 0; i < n; i++) {
        void* src = byteOffset(result, i * size);
        VectorPush(v, src);
    }

    // clean up
    VecFree(v, arr1);
    VecFree(v, arr2);
}

Arena* VectorArena(Vector *v) {
    return v->_arena;
}

// Clone a vector into a new arena
Vector* VectorClone(Vector* source, Arena* a) {
    // this could be optimised, but we need to relocate all pointers properly if we do.
    if (source == nullptr) return nullptr;
    if (a == nullptr) a = MMCurrent();

    auto len = VectorLength(source);
    auto elemSize = VectorElementSize(source);
    auto result = VectorAllocateArena(a, elemSize);
    VectorPreallocate(result, len);
    for (size_t i = 0; i < len; i++) {
        var src = PtrOfElem(source, i);
        if (src == nullptr) continue;

        var dst = PtrOfElem(result, i);
        if (dst == nullptr) continue;

        writeValue(dst, 0, src, elemSize);
    }
    return result;
}

uint32_t VectorElementSize(Vector * v) {
    if (v == nullptr) return 0;
    return v->ElementByteSize;
}
