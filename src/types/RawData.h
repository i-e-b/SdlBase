#pragma once

#ifndef RawData_h
#define RawData_h

// A bunch of inline helper methods for dealing with anonymous data types

inline int readInt(void* ptr) {
    return *((int*)ptr);
}
inline void writeInt(void *ptr, int data) {
    *((int*)ptr) = data;
}
inline void writeIntPrefixValue(void *dst, int priority, void* data, int length) {
    char* src = (char*)data;
    int* priorityPtr = (int*)dst;
    char* elemPtr = (char*)dst;
    elemPtr += sizeof(int);

    *priorityPtr = priority;
    for (int i = 0; i < length; i++) {
        *(elemPtr++) = *(src++);
    }
}
inline void readIntPrefixValue(void *dest, void* vecEntry, int length) {
    char* dst = (char*)dest;
    char* src = (char*)vecEntry;
    src += sizeof(int);

    for (int i = 0; i < length; i++) {
        *(dst++) = *(src++);
    }
}
inline void * byteOffset(void *ptr, size_t byteOffset) {
    auto x = (size_t)ptr;
    x += byteOffset;
    return (void*)x;
}
inline unsigned short readUshort(void* ptr, size_t byteOffset) {
    char* x = (char*)ptr;
    x += byteOffset;
    return *((unsigned short*)x);
}
inline void writeUshort(void *ptr, size_t byteOffset, unsigned short data) {
    char* x = (char*)ptr;
    x += byteOffset;
    *(unsigned short*)x = data;
}
inline void* readPtr(void* ptr, size_t byteOffset) {
    char* x = (char*)ptr;
    x += byteOffset;
    size_t v = *((size_t*)x);
    return (void*)v;
}
inline unsigned int readUint(void* ptr, size_t byteOffset) {
    char* x = (char*)ptr;
    x += byteOffset;
    return *((unsigned int*)x);
}
inline void writeUint(void *ptr, size_t byteOffset, unsigned int data) {
    char* x = (char*)ptr;
    x += byteOffset;
    *(unsigned int*)x = data;
}
inline void writePtr(void *ptr, size_t byteOffset, void* data) {
    char* x = (char*)ptr;
    x += byteOffset;
    *(size_t*)x = (size_t)data;
}
inline void writeValue(void *ptr, size_t byteOffset, void* data, int length) {
    char* dst = (char*)ptr;
    dst += byteOffset;
    char* src = (char*)data;

    for (int i = 0; i < length; i++) {
        *(dst++) = *(src++);
    }
}
inline void writeValue(void *ptr, size_t byteOffset, void* data, uint32_t length) {
    char* dst = (char*)ptr;
    dst += byteOffset;
    char* src = (char*)data;

    for (uint32_t i = 0; i < length; i++) {
        *(dst++) = *(src++);
    }
}
inline void copyAnonArray(void *dstPtr, int dstIndex, void* srcPtr, int srcIndex, size_t length) {
    char* dst = (char*)dstPtr;
    dst += dstIndex * length;
    char* src = (char*)srcPtr;
    src += srcIndex * length;

    for (size_t i = 0; i < length; i++) {
        *(dst++) = *(src++);
    }
}
inline void swapMem(void * const a, void * const b, int n) {
    auto* p = (unsigned char*)a;
    auto* q = (unsigned char*)b;
    unsigned char* const sentry = (unsigned char*)a + n;

    for (; p < sentry; ++p, ++q) {
        const unsigned char t = *p;
        *p = *q;
        *q = t;
    }
}
inline void swapMem(void * const a, void * const b, uint32_t n) {
    auto* p = (unsigned char*)a;
    auto* q = (unsigned char*)b;
    unsigned char* const sentry = (unsigned char*)a + n;

    for (; p < sentry; ++p, ++q) {
        const unsigned char t = *p;
        *p = *q;
        *q = t;
    }
}

inline uint32_t NextPow2(uint32_t c) {
    c--;
    c |= c >> 1;
    c |= c >> 2;
    c |= c >> 4;
    c |= c >> 8;
    c |= c >> 16;
    return ++c;
}


#endif
