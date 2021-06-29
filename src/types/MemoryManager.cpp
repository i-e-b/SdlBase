#include "MemoryManager.h"
#include "Vector.h"

#include <cstdlib>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#ifdef ARENA_DEBUG
#include <iostream>
#endif

static volatile Vector* MEMORY_STACK = nullptr;
static volatile int LOCK = 0;

typedef Arena* ArenaPtr;

RegisterVectorStatics(Vec)
RegisterVectorFor(ArenaPtr, Vec)

// Ensure the memory manager is ready. It starts with an empty stack
void StartManagedMemory() {
    if (LOCK != 0) return; // weak protection, yes.
    if (MEMORY_STACK != nullptr) return;
    LOCK = 1;

    MEMORY_STACK = VecAllocateArena_ArenaPtr(NewArena((128 KILOBYTES)));

    LOCK = 0;
}
// Close all arenas and return to stdlib memory
void ShutdownManagedMemory() {
    if (LOCK != 0) return;
    if (MEMORY_STACK == nullptr) return;

    auto* vec = (Vector*)MEMORY_STACK;
    ArenaPtr a = nullptr;
    while (VecPop_ArenaPtr(vec, &a)) {
        DropArena(&a);
    }
    VecDeallocate(vec);
    MEMORY_STACK = nullptr;

    LOCK = 0;
}

// Start a new arena, keeping memory and state of any existing ones
bool MMPush(size_t arenaMemory) {
    if (MEMORY_STACK == nullptr) return false;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
    while (LOCK != 0) {}
#pragma clang diagnostic pop
    LOCK = 1;

    auto* vec = (Vector*)MEMORY_STACK;
    auto a = NewArena(arenaMemory);
    bool result = false;
    if (a != nullptr) {
        result = VecPush_ArenaPtr(vec, a);
        if (!result) DropArena(&a);
    }

    LOCK = 0;
    return result;
}

// Deallocate the most recent arena, restoring the previous
void MMPop() {
    if (MEMORY_STACK == nullptr) return;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
    while (LOCK != 0) {}
#pragma clang diagnostic pop
    LOCK = 1;

    auto* vec = (Vector*)MEMORY_STACK;
    ArenaPtr a = nullptr;
    if (VecPop_ArenaPtr(vec, &a)) {
        DropArena(&a);
    }

    LOCK = 0;
}

// Deallocate the most recent arena, copying a data item to the next one down (or permanent memory if at the bottom of the stack)
void* MMPopReturn(void* ptr, size_t size) {
    if (MEMORY_STACK == nullptr) return nullptr;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
    while (LOCK != 0) {}
#pragma clang diagnostic pop
    LOCK = 1;

    void* result;
    auto* vec = (Vector*)MEMORY_STACK;
    ArenaPtr a = nullptr;
    if (VecPop_ArenaPtr(vec, &a)) {
        if (VecPeek_ArenaPtr(vec, &a)) { // there is another arena. Copy there
            result = CopyToArena(ptr, size, a);
        } else { // no more arenas. Dump in regular memory
            result = MakePermanent(ptr, size);
        }
        DropArena(&a);
    } else { // nothing to pop. Raise null to signal stack underflow
        result = nullptr;
    }

    LOCK = 0;
    return result;
}

// Return the current arena, or nullptr if none pushed
// TODO: if nothing pushed, push a new small arena
Arena* MMCurrent() {
    if (MEMORY_STACK == nullptr) return nullptr;
#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
    while (LOCK != 0) {}
#pragma clang diagnostic pop
    LOCK = 1;

    auto* vec = (Vector*)MEMORY_STACK;
    ArenaPtr result = nullptr;
    VecPeek_ArenaPtr(vec, &result);

    LOCK = 0;
    return result;
}

// Allocate memory array, cleared to zeros
void* mcalloc(int count, size_t size) {
    ArenaPtr a = MMCurrent();
    if (a != nullptr) {
        return ArenaAllocateAndClear(a, count*size);
    } else {
        return calloc(count, size);
    }
}

// Free memory
void mfree(void* ptr) {
    if (ptr == nullptr) return;
    // we might not be freeing from the current arena, so this can get complex
    ArenaPtr a = MMCurrent();
    if (a == nullptr) { // no arenas. stdlib free
        free(ptr);
        return;
    }
    if (ArenaContainsPointer(a, ptr)) { // in the most recent arena
        ArenaDereference(a, ptr);
        return;
    }

    // otherwise, scan through all the arenas until we find it
    // it might be simpler to leak the memory and let the arena get cleaned up whenever

#pragma clang diagnostic push
#pragma ide diagnostic ignored "LoopDoesntUseConditionVariableInspection"
    while (LOCK != 0) {}
#pragma clang diagnostic pop
    LOCK = 1;

    auto* vec = (Vector*)MEMORY_STACK;
    int count = VecLength(vec);
    for (int i = 0; i < count; i++) {
        ArenaPtr af = *VecGet_ArenaPtr(vec, i);
        if (af == nullptr) continue;
        if (ArenaContainsPointer(af, ptr)) {
            ArenaDereference(af, ptr);
            return;
        }
    }
    // never found it. Either bad call or we've leaked some memory

#ifdef ARENA_DEBUG
    std::cout << "mfree failed. Memory leaked.\n";
#endif

    LOCK = 0;
}
#pragma clang diagnostic pop