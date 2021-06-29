#include "HashMap.h"
#include "String.h"
#include "MemoryManager.h"
#include "RawData.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
// Fixed sizes -- these are structural to the code and must not change
const unsigned int MAX_BUCKET_SIZE = 1<<30; // safety limit for scaling the buckets
const unsigned int SAFE_HASH = 0x80000000; // just in case you get a zero result

// Tuning parameters: have a play if you have performance or memory issues.
const unsigned int MIN_BUCKET_SIZE = 64; // default size used if none given
const float LOAD_FACTOR = 0.8f; // higher is more memory efficient. Lower is faster, to a point.

//#define AGGRESSIVE_SCALING 1

// Entry in the hash-table
// The actual entries are tagged on the end of the entry
typedef struct HashMap_Entry {
    uint32_t hash; // NOTE: a hash value equal to zero marks an empty slot
} HashMap_Entry;

typedef struct HashMap {
    // Storage and types
    Vector* buckets; // this is a Vector<HashMap_Entry + keyData + valueData>
    Arena* memory; // location for allocating new memory.

    int KeyByteSize; // byte length of the key
    int ValueByteSize; // byte length of the value

    // Hashmap metrics
    unsigned int count;
    unsigned int countMod;
    unsigned int countUsed;
    unsigned int growAt;
    unsigned int shrinkAt;

    bool IsValid; // if false, the hash map has failed

    // Should return true IFF the two key objects are equal
    bool(*KeyComparer)(void* key_A, void* key_B);

    // Should return a unsigned 32bit hash value for the given key
    unsigned int(*GetHash)(void* key);

} HashMap;

bool HashMapIsValid(HashMap *h) {
    if (h == nullptr) return false;
    if (!VectorIsValid(h->buckets)) return false;
    return h->IsValid;
}

bool ResizeNext(HashMap * h); // defined below

inline uint32_t DistanceToInitIndex(HashMap * h, uint32_t indexStored, HashMap_Entry* entry) {
    auto indexInit = entry->hash & h->countMod;
    if (indexInit <= indexStored) return indexStored - indexInit;
    return indexStored + (h->count - indexInit);
}

inline void* KeyPtr(HashMap_Entry* e) {
    return byteOffset(e, sizeof(HashMap_Entry));
}
inline void* ValuePtr(HashMap* h, HashMap_Entry* e) {
    return byteOffset(e, sizeof(HashMap_Entry) + h->KeyByteSize);
}

inline bool SwapOut(Vector* vec, uint32_t idx, HashMap_Entry* newEntry) {
    if (vec == nullptr) return false;

    auto size = VectorElementSize(vec);

    // use push/pop to do temp allocation
    if (!VectorPush(vec, newEntry)) return false;

    auto temp = VectorGet(vec, -1);

    if (!VectorSet(vec, (int)idx, newEntry, temp)) {
        VectorPop(vec, nullptr);
        return false;
    }
    writeValue(newEntry, 0, temp, size);
    VectorPop(vec, nullptr);
    return true;
}

bool PutInternal(HashMap * h, HashMap_Entry* entry, bool canReplace, bool checkDuplicates) {
    uint32_t indexInit = entry->hash & h->countMod;
    uint32_t probeCurrent = 0;

    for (uint32_t i = 0; i < h->count; i++) {
        auto indexCurrent = (int)((indexInit + i) & h->countMod);

        if (!VectorIsValid(h->buckets))
            return false;
        auto current = (HashMap_Entry*)VectorGet(h->buckets, indexCurrent);
        if (current == nullptr) return false; // internal failure

        if (current->hash == 0) {
            h->countUsed++;
            VectorSet(h->buckets, indexCurrent, entry, nullptr);
            return true;
        }

        if (checkDuplicates && (entry->hash == current->hash)
            && h->KeyComparer(KeyPtr(entry), KeyPtr(current))
            ) {
            if (!canReplace) return false;

            if (!VectorIsValid(h->buckets)) return false;

            VectorSet(h->buckets, indexCurrent, entry, nullptr);
            return true;
        }

        // Perform the core robin-hood balancing
        auto probeDistance = DistanceToInitIndex(h, indexCurrent, current);
        if (probeCurrent > probeDistance) {
            probeCurrent = probeDistance;
            if (!SwapOut(h->buckets, indexCurrent, entry))
                return false;
        }
        probeCurrent++;
    }
    // need to grow?
    // Trying recursive insert:
    if (!ResizeNext(h)) return false;
    return PutInternal(h, entry, canReplace, checkDuplicates);
}

bool Resize(HashMap * h, size_t newSize, bool autoSize) {
    auto oldCount = h->count;
    auto oldBuckets = h->buckets;

    if (newSize > 0 && newSize < MIN_BUCKET_SIZE) newSize = MIN_BUCKET_SIZE;
    if (newSize > MAX_BUCKET_SIZE) newSize = MAX_BUCKET_SIZE;

    h->count = newSize;
    h->countMod = newSize - 1;

    auto newBuckets = VectorAllocateArena(h->memory, sizeof(HashMap_Entry) + h->KeyByteSize + h->ValueByteSize);
    if (!VectorIsValid(newBuckets) || !VectorPreallocate(newBuckets, newSize)) return false;

    h->buckets = newBuckets;

    h->growAt = autoSize ? (uint32_t)((float)newSize * LOAD_FACTOR) : newSize;
    h->shrinkAt = autoSize ? newSize >> 2 : 0;

    h->countUsed = 0;


    // if the old buckets are null or empty, there are no values to copy
    if (!VectorIsValid(oldBuckets)) { return true; }

    // old values need adding to the new hashmap
    if (newSize > 0) {
        for (uint32_t i = 0; i < oldCount; i++) {
            // Read and validate old bucket entry
            auto oldEntry = (HashMap_Entry*)VectorGet(oldBuckets, (int)i);
            if (oldEntry == nullptr || oldEntry->hash == 0) continue;

            // Copy the old entry into the new bucket vector
            PutInternal(h, oldEntry, false, false);
        }
    }

    VectorDeallocate(oldBuckets);
    return true;
}


void HashMapPurge(HashMap *h) {
    auto size = NextPow2((uint32_t)((float)(h->countUsed) / LOAD_FACTOR));
    Resize(h, size, true);
}

bool ResizeNext(HashMap * h) {
    // mild scaling can save memory, but resizing is very expensive

#ifdef AGGRESSIVE_SCALING
    // Aggressive scaling
    unsigned long size = (unsigned long)h->count * 2;
    if (h->count < 8192) size = (unsigned long)h->count * h->count;
    if (size < MIN_BUCKET_SIZE) size = MIN_BUCKET_SIZE;
    return Resize(h, (uint32_t)size, true);
#else
    // Mild scaling
    return Resize(h, h->count == 0 ? 32 : h->count * 2, true);
#endif


}


#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedLocalVariable"
HashMap* HashMapAllocateArena(Arena* a, unsigned int size, int keyByteSize, int valueByteSize, bool(*keyComparerFunc)(void* /*key_A*/, void* /*key_B*/), unsigned int(*getHashFunc)(void* /*key*/)) {
#pragma clang diagnostic pop
    if (a == nullptr) return nullptr;
    auto result = (HashMap*)ArenaAllocateAndClear(a, sizeof(HashMap));
    if (result == nullptr) return nullptr;
    result->memory = a;
    result->KeyByteSize = keyByteSize;
    result->ValueByteSize = valueByteSize;
    result->KeyComparer = keyComparerFunc;
    result->GetHash = getHashFunc;
    result->buckets = nullptr; // created in `Resize`
    result->IsValid = Resize(result, (uint32_t)NextPow2(size), false);
    return result;
}


#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedLocalVariable"
HashMap* HashMapAllocate(unsigned int size, int keyByteSize, int valueByteSize, bool(*keyComparerFunc)(void* /*key_A*/, void* /*key_B*/), unsigned int(*getHashFunc)(void* /*key*/)) {
    return HashMapAllocateArena(MMCurrent(), size, keyByteSize, valueByteSize, keyComparerFunc, getHashFunc);
}
#pragma clang diagnostic pop

void HashMapDeallocate(HashMap * h) {
    if (h == nullptr) return;
    h->IsValid = false;
    h->count = 0;
    if (h->buckets != nullptr) VectorDeallocate(h->buckets);
    ArenaDereference(h->memory, h);
}

bool Find(HashMap* h, void* key, uint32_t* index, HashMap_Entry** found) {
    *index = 0;
    if (h == nullptr) return false;
    if (h->countUsed <= 0) return false;
    //if (!VectorIsValid(h->buckets)) return false;

    uint32_t hash = h->GetHash(key);
    uint32_t indexInit = hash & h->countMod;
    uint32_t probeDistance = 0;

    for (uint32_t i = 0; i < h->count; i++) {
        *index = (indexInit + i) & h->countMod;
        auto res = (HashMap_Entry*)VectorGet(h->buckets, (int)(*index));
        if (res == nullptr) return false; // internal failure

        auto keyData = KeyPtr(res);

        // found?
        if ((hash == res->hash) && h->KeyComparer(key, keyData)) {
            if (found != nullptr) *found = res;
            return true;
        }

        // not found, probe further
        if (res->hash != 0) probeDistance = DistanceToInitIndex(h, *index, res);

        if (i > probeDistance) break;
    }

    return false;
}

bool HashMapGet(HashMap* h, void* key, void** outValue) {
    if (h == nullptr) return false;
    // Find the entry index
    uint32_t index = 0;
    HashMap_Entry* res = nullptr;
    if (!Find(h, key, &index, &res)) return false;

    // look up the entry
    if (res == nullptr) {
        return false;
    }

    // look up the value
    if (outValue != nullptr) *outValue = ValuePtr(h, res);
    return true;
}

inline HashMap_Entry* HashMapAllocEntry(HashMap* h) {
    if (h->memory == nullptr) return nullptr;
    return (HashMap_Entry*)ArenaAllocateAndClear(h->memory, sizeof(HashMap_Entry) + h->KeyByteSize + h->ValueByteSize);
}

inline void HashMapFreeEntry(HashMap* h, HashMap_Entry* e) {
    ArenaDereference(h->memory, e);
}

bool HashMapPut(HashMap* h, void* key, void* value, bool canReplace) {
    if (h == nullptr) return false;
    // Check to see if we need to grow
    if (h->countUsed >= h->growAt) {
        if (!ResizeNext(h)) return false;
    }

    uint32_t safeHash = h->GetHash(key);
    if (safeHash == 0) safeHash = SAFE_HASH; // can't allow hash of zero
    
    // Write the entry into the hashmap
    auto entry = HashMapAllocEntry(h);
    if (entry == nullptr) return false;

    entry->hash = safeHash;
    writeValue(KeyPtr(entry), 0, key, h->KeyByteSize);
    writeValue(ValuePtr(h, entry), 0, value, h->ValueByteSize);

    auto OK = PutInternal(h, entry, canReplace, true);

    HashMapFreeEntry(h, entry);
    return OK;
}

Vector *HashMapAllEntries(HashMap* h) {
    auto result = VectorAllocateArena(h->memory, sizeof(HashMap_KVP));
    if (!VectorIsValid(h->buckets)) return result;

    for (uint32_t i = 0; i < h->count; i++) {
        // Read and validate entry
        auto ent = (HashMap_Entry*)VectorGet(h->buckets, (int)i);
        if (ent->hash == 0) continue;

        // Look up pointers to the data
        auto keyPtr = KeyPtr(ent);
        auto valuePtr = ValuePtr(h, ent);

        // Add Key-Value pair to output
        auto kvp = HashMap_KVP { keyPtr, valuePtr };
        VectorPush(result, &kvp);
    }
    return result;
}

bool HashMapRemove(HashMap* h, void* key) {
    uint32_t index;
    if (!Find(h, key, &index, nullptr)) return false;

    for (uint32_t i = 0; i < h->count; i++) {
        auto curIndex = (index + i) & h->countMod;
        auto nextIndex = (index + i + 1) & h->countMod;

        auto res = (HashMap_Entry*)VectorGet(h->buckets, (int)nextIndex);
        if (res == nullptr) return false; // internal failure

        if ((res->hash == 0) || (DistanceToInitIndex(h, nextIndex, res) == 0))
        {
            auto empty = HashMap_Entry();
            VectorSet(h->buckets, (int)curIndex, &empty, nullptr);

            if (--(h->countUsed) == h->shrinkAt) Resize(h, h->shrinkAt, true);

            return true;
        }

        VectorSwap(h->buckets, curIndex, nextIndex);
    }

    return false;
}

void HashMapClear(HashMap * h) {
    Resize(h, 0, true);
}

unsigned int HashMapCount(HashMap * h) {
    return h->countUsed;
}

// Default comparer and hash functions:

bool HashMapStringKeyCompare(void* key_A, void* key_B) {
    auto A = *((StringPtr*)key_A);
    auto B = *((StringPtr*)key_B);
    return StringAreEqual(A, B);
}
uint32_t HashMapStringKeyHash(void* key) {
    auto A = *((StringPtr*)key);
    return StringHash(A);
}
bool HashMapIntKeyCompare(void* key_A, void* key_B) {
    auto A = *((uint32_t*)key_A);
    auto B = *((uint32_t*)key_B);
    return A == B;
}
uint32_t HashMapIntKeyHash(void* key) {
    auto A = *((uint32_t*)key);
    return A | 0xA0000000;
}

#pragma clang diagnostic pop