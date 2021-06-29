#include "ArenaAllocator.h"
#include "RawData.h"

#include <cstdlib>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#ifdef ARENA_DEBUG
#include <iostream>
#endif

// maximum number of references in a zone before we give up.
#define ZONE_MAX_REFS 65000

typedef struct Arena {
#ifdef ARENA_DEBUG
    // Diagnostic marker
    bool _marked;
#endif

    // Bottom of free memory (after arena management is taken up)
    void* _start;

    // Top of memory
    void* _limit;

    // Pointer to array of ushort, length is equal to _zoneCount.
    // Each element is offset of next pointer to allocate. Zero indicates an empty zone.
    // This is also the base of allocated memory
    uint16_t* _headsPtr;

    // Pointer to array of ushort, length is equal to _arenaCount.
    // Each element is number of references claimed against the arena.
    uint16_t* _refCountsPtr;

    // The most recent arena that had a successful alloc or clear
    int _currentZone;

    // Count of available arenas. This is the limit of memory
    int _zoneCount;
} Arena;

// Create a new arena for memory management. Size is the maximum size for the whole
// arena. Fragmentation may make the usable size smaller. Size should be a multiple of ARENA_ZONE_SIZE
Arena* NewArena(size_t size) {
    int expectedZoneCount = (int)(size / ARENA_ZONE_SIZE) + 1;

    auto realMemory = calloc(1, size + ARENA_ZONE_SIZE);
    if (realMemory == nullptr) return nullptr;

    auto result = (Arena*)calloc(1, sizeof(Arena));
    if (result == nullptr) {
        free(realMemory);
        return nullptr;
    }

    result->_start = realMemory;
    result->_limit = byteOffset(realMemory, size - 1);
    
#ifdef ARENA_DEBUG
    result->_marked = false;
#endif

    // with 64KB arenas (ushort) and 1GB of RAM, we get 16384 arenas.
    // recording only heads and refs would take 64KB of management space
    // recording heads, refs and back-step (an optimisation for very short-lived items) would use 96KB of management space.
    // This seems pretty reasonable.
    result->_zoneCount = expectedZoneCount - 1;
    result->_currentZone = 0;

    // Allow space for arena tables, store adjusted base
    auto sizeOfTables = sizeof(uint16_t) * result->_zoneCount;
    result->_headsPtr = (uint16_t*)result->_start;
    result->_refCountsPtr = (uint16_t*)byteOffset(result->_start, sizeOfTables);

    // shrink space for headers
    result->_start = byteOffset(result->_start, sizeOfTables * 2);

    // zero-out the tables
    auto zeroPtr = result->_headsPtr;
    while (zeroPtr < result->_start) {
        writeUshort(zeroPtr, 0, 0);
        zeroPtr += 1;
    }

    return result;
}

// Call to drop an arena, deallocating all memory it contains
void DropArena(Arena** a) {
    if (a == nullptr) return;

    auto ptr = *a;
	*a = nullptr; // kill the arena reference
    if (ptr == nullptr) return;

    if (ptr->_headsPtr != nullptr) { // delete contained memory
        free(ptr->_headsPtr);
        ptr->_headsPtr = nullptr;
        ptr->_start = nullptr;
        ptr->_limit = nullptr;
    }

    free(ptr); // Free the arena reference itself
}

void TraceArena(Arena* a, bool traceOn) {
#ifdef ARENA_DEBUG
    a->_marked = traceOn;
#endif
}

// Copy arena data out to system-level memory. Use this for very long-lived data
void* MakePermanent(void* data, size_t length) {
    if (length < 1) return nullptr;
    if (data == nullptr) return nullptr;

    void* perm = malloc(length);
    if (perm == nullptr) return nullptr;

    copyAnonArray(perm, 0, data, 0, length);

    return perm;
}

// Copy data from one arena to another. Use this for return values
void* CopyToArena(void* srcData, size_t length, Arena* target) {
    if (srcData == nullptr) return nullptr;
    if (length < 1) return nullptr;
    if (target == nullptr) return nullptr;

    auto dstData = ArenaAllocate(target, length);
    if (dstData == nullptr) return nullptr;

    copyAnonArray(dstData, 0, srcData, 0, length);

    return dstData;
}


uint16_t GetHead(Arena* a, int zoneIndex) {
    return readUshort(a->_headsPtr, zoneIndex * sizeof(uint16_t));
}
uint16_t GetRefCount(Arena* a, int zoneIndex) {
    return readUshort(a->_refCountsPtr, zoneIndex * sizeof(uint16_t));
}
void SetHead(Arena* a, int zoneIndex, uint16_t val) {
    writeUshort(a->_headsPtr, zoneIndex * sizeof(uint16_t), val);
}
void SetRefCount(Arena* a, int zoneIndex, uint16_t val) {
    writeUshort(a->_refCountsPtr, zoneIndex * sizeof(uint16_t), val);
}

// Allocate memory of the given size
void* ArenaAllocate(Arena* a, size_t byteCount) {
    if (byteCount > ARENA_ZONE_SIZE) return nullptr; // Invalid allocation -- beyond max size.
    if (a == nullptr) return nullptr;

#ifdef ARENA_DEBUG
    if (a->_marked) {
        std::cout << "A@" << a->_headsPtr << ";S" << byteCount << "\n";
    }
#endif

    auto maxOff = ARENA_ZONE_SIZE - byteCount;
    auto zoneCount = a->_zoneCount;

    // scan for first arena where there is enough room
    // we can either start from scratch each time, start from last success, or last emptied
    for (int seq = 0; seq < zoneCount; seq++) {
        auto i = (seq + a->_currentZone) % zoneCount; // simple scan from last active, looping back if needed

        if (GetHead(a, i) > maxOff) continue; // no room in this slot

        // found a slot where it will fit
        a->_currentZone = i;
        size_t result = GetHead(a, i); // new pointer
        SetHead(a, i, (uint16_t) (result + byteCount)); // advance pointer to end of allocated data

        auto oldRefs = GetRefCount(a, i);
        SetRefCount(a, i, oldRefs + 1); // increase arena ref count

        return byteOffset(a->_start, result + (i * ARENA_ZONE_SIZE)); // turn the offset into an absolute position
    }

    // found nothing -- out of memory!
    return nullptr;
}

void* ArenaAllocateAndClear(Arena* a, size_t byteCount) {
    char* res = (char*)ArenaAllocate(a, byteCount);
    if (res == nullptr) return nullptr;
    for (size_t i = 0; i < byteCount; i++) {
        res[i] = 0;
    }
    return (void*)res;
}


int ZoneForPtr(Arena* a, void* ptr) {
    if (ptr < a->_start || ptr > a->_limit) return -1;

    ptrdiff_t rawOffset = (ptrdiff_t)ptr - (ptrdiff_t)a->_start;
    ptrdiff_t zone = rawOffset / ARENA_ZONE_SIZE;
    if (zone < 0 || zone >= a->_zoneCount) return -1;
    return (int)zone;
}

bool ArenaContainsPointer(Arena* a, void* ptr) {
    if (a == nullptr) return false;
    if (ptr < a->_start || ptr > a->_limit) return false;

    return true;
}

// Remove a reference to memory. When no references are left, the memory may be deallocated
bool ArenaDereference(Arena* a, void* ptr) {
    if (a == nullptr) return false;
    if (ptr == nullptr) return false;

#ifdef ARENA_DEBUG
    if (a->_marked) {
        std::cout << "D@" << a->_headsPtr << "\n";
    }
#endif

    auto zone = ZoneForPtr(a, ptr);
    if (zone < 0) return false;

    auto refCount = GetRefCount(a, zone);
    if (refCount == 0) return false; // Over-free. Fix your code.

    refCount--;
    SetRefCount(a, zone, refCount);

    // If no more references, free the block
    if (refCount == 0) {
        SetHead(a, zone, 0);
        if (zone < a->_currentZone) a->_currentZone = zone; // keep allocations packed in low memory. Is this worth it?
    }
    return true;
}

// Add a reference to memory, to delay deallocation. When no references are left, the memory may be deallocated
bool ArenaReference(Arena* a, void* ptr) {
    if (a == nullptr) return false;
    if (ptr == nullptr) return false;

#ifdef ARENA_DEBUG
    if (a->_marked) {
        std::cout << "R@" << a->_headsPtr << "\n";
    }
#endif

    auto zone = ZoneForPtr(a, ptr);
    if (zone < 0) return false;

    auto oldRefs = GetRefCount(a, zone);
    if (oldRefs >= ZONE_MAX_REFS) return false; // saturated references. Fix your code.

    SetRefCount(a, zone, oldRefs + 1);
    return true;
}

// Read statistics for this Arena. Pass `NULL` for anything you're not interested in.
void ArenaGetState(Arena* a, size_t* allocatedBytes, size_t* unallocatedBytes,
    int* occupiedZones, int* emptyZones, int* totalReferenceCount, size_t* largestContiguous) {
    if (a == nullptr) return;

    size_t allocated = 0;
    size_t unallocated = 0;
    int occupied = 0;
    int empty = 0;
    int totalReferences = 0;
    size_t largestFree = 0;

    auto zoneCount = a->_zoneCount;

    for (int i = 0; i < zoneCount; i++) {
        auto zoneRefCount = GetRefCount(a, i);
        auto zoneHead = GetHead(a, i);
        totalReferences += zoneRefCount;

        if (zoneHead > 0) occupied++;
        else empty++;

        unsigned free = ARENA_ZONE_SIZE - zoneHead;
        allocated += zoneHead;
        unallocated += free;
        if (free > largestFree) largestFree = free;
    }

    if (allocatedBytes != nullptr) *allocatedBytes = allocated;
    if (unallocatedBytes != nullptr) *unallocatedBytes = unallocated;
    if (occupiedZones != nullptr) *occupiedZones = occupied;
    if (emptyZones != nullptr) *emptyZones = empty;
    if (totalReferenceCount != nullptr) *totalReferenceCount = totalReferences;
    if (largestContiguous != nullptr) *largestContiguous = largestFree;
}

// Get an offset into the arena for a pointer to memory
uint32_t ArenaPtrToOffset(Arena* a, void* ptr) {
    if (!ArenaContainsPointer(a, ptr)) return 0;

    auto base = (size_t)(a->_start);
    auto actual = (size_t)ptr;

    if (base >= actual) return 0;

    return (actual - base) + 1; // zero is a failure case
}

// Get a raw memory pointer from an offset into an arena
void* ArenaOffsetToPtr(Arena* a, uint32_t offset) {
    auto base = (size_t)(a->_start);
    size_t actual = base + (size_t)offset - 1;
    if (!ArenaContainsPointer(a, (void*)actual)) return nullptr; // not a valid answer
    return (void*)actual;
}


#pragma clang diagnostic pop