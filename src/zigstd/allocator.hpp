/* Zig std.mem.Allocator and the few allocator implementations Ghostty's
 * terminal code and tests use, reduced to what those call sites need.
 * Derived from Zig lib/std/mem/Allocator.zig, std/heap and std/testing
 * Copyright (c) Zig contributors — MIT License, see THIRD_PARTY_NOTICES.md
 *
 * Wisp: `Allocator.Error!T` is a null return (OutOfMemory). resize/remap
 * are not carried over; callers that grow use alloc + copy + free as Zig's
 * fallback does.
 */

#pragma once
#ifndef WISP_ZIGSTD_ALLOCATOR_HPP
#define WISP_ZIGSTD_ALLOCATOR_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

extern "C" __declspec(dllimport) void *__stdcall VirtualAlloc(void *, size_t, unsigned long, unsigned long);
extern "C" __declspec(dllimport) int __stdcall VirtualFree(void *, size_t, unsigned long);

namespace wisp {
namespace zigstd {

struct Allocator {
    struct VTable {
        /* Return a pointer to `len` bytes with alignment `alignment`, or null. */
        uint8_t *(*alloc)(void *ctx, size_t len, size_t alignment);
        /* Free memory previously returned by alloc with the same len/alignment. */
        void (*free)(void *ctx, uint8_t *memory, size_t len, size_t alignment);
    };

    void *ptr;
    const VTable *vtable;

    uint8_t *alignedAlloc(size_t len, size_t alignment) const {
        if (len == 0) return (uint8_t *)(uintptr_t)alignment; /* Zig: zero-length is a non-null sentinel */
        return vtable->alloc(ptr, len, alignment);
    }
    uint8_t *alloc(size_t len) const { return alignedAlloc(len, 1); }
    void free(uint8_t *memory, size_t len, size_t alignment = 1) const {
        if (len == 0) return;
        vtable->free(ptr, memory, len, alignment);
    }

    template <typename T>
    T *allocT(size_t n) const {
        return (T *)alignedAlloc(n * sizeof(T), alignof(T));
    }
    template <typename T>
    void freeT(T *p, size_t n) const {
        free((uint8_t *)p, n * sizeof(T), alignof(T));
    }

    /* Allocator.create / destroy: storage only, no constructor run. */
    template <typename T>
    T *create() const {
        return (T *)alignedAlloc(sizeof(T), alignof(T));
    }
    template <typename T>
    void destroy(T *p) const {
        free((uint8_t *)p, sizeof(T), alignof(T));
    }

    /* Allocator.dupe(u8, ...) */
    uint8_t *dupe(const uint8_t *src, size_t len) const {
        uint8_t *d = alloc(len);
        if (d && len) memcpy(d, src, len);
        return d;
    }
};

/* std.heap.c_allocator equivalent (aligned CRT heap). */
namespace detail {
inline uint8_t *cAlloc(void *, size_t len, size_t alignment) {
    return (uint8_t *)_aligned_malloc(len, alignment < sizeof(void *) ? sizeof(void *) : alignment);
}
inline void cFree(void *, uint8_t *memory, size_t, size_t) { _aligned_free(memory); }

/* std.heap.page_allocator: VirtualAlloc, zero-filled and page-aligned. */
inline uint8_t *pageAlloc(void *, size_t len, size_t alignment) {
    (void)alignment; /* VirtualAlloc returns 64 KiB-aligned regions */
    return (uint8_t *)::VirtualAlloc(nullptr, len, 0x00001000 | 0x00002000, 0x04);
}
inline void pageFree(void *, uint8_t *memory, size_t, size_t) { ::VirtualFree(memory, 0, 0x00008000); }
} /* namespace detail */

inline Allocator c_allocator() {
    static const Allocator::VTable vt = {detail::cAlloc, detail::cFree};
    Allocator a = {nullptr, &vt};
    return a;
}

inline Allocator page_allocator() {
    static const Allocator::VTable vt = {detail::pageAlloc, detail::pageFree};
    Allocator a = {nullptr, &vt};
    return a;
}

/* std.heap.FixedBufferAllocator: bump allocation within a buffer; free
 * only rolls back the most recent allocation. */
struct FixedBufferAllocator {
    uint8_t *buffer;
    size_t buffer_len;
    size_t end_index;

    FixedBufferAllocator(uint8_t *buf, size_t len) : buffer(buf), buffer_len(len), end_index(0) {}

    static uint8_t *allocFn(void *ctx, size_t len, size_t alignment) {
        FixedBufferAllocator *self = (FixedBufferAllocator *)ctx;
        const uintptr_t base = (uintptr_t)self->buffer + self->end_index;
        const uintptr_t aligned = (base + alignment - 1) & ~(uintptr_t)(alignment - 1);
        const size_t adjusted_index = self->end_index + (size_t)(aligned - base);
        if (adjusted_index + len > self->buffer_len) return nullptr;
        self->end_index = adjusted_index + len;
        return self->buffer + adjusted_index;
    }
    static void freeFn(void *ctx, uint8_t *memory, size_t len, size_t) {
        FixedBufferAllocator *self = (FixedBufferAllocator *)ctx;
        if (memory + len == self->buffer + self->end_index) self->end_index -= len;
    }

    Allocator allocator() {
        static const Allocator::VTable vt = {allocFn, freeFn};
        Allocator a = {this, &vt};
        return a;
    }
};

/* std.testing.FailingAllocator: fails the allocation at `fail_index`
 * (counting from zero) and every one after. */
struct FailingAllocator {
    Allocator internal_allocator;
    size_t alloc_index;
    size_t fail_index;
    size_t allocated_bytes;
    size_t freed_bytes;
    size_t allocations;
    size_t deallocations;
    bool has_induced_failure;

    FailingAllocator(Allocator internal, size_t fail_index_)
        : internal_allocator(internal), alloc_index(0), fail_index(fail_index_), allocated_bytes(0), freed_bytes(0),
          allocations(0), deallocations(0), has_induced_failure(false) {}

    static uint8_t *allocFn(void *ctx, size_t len, size_t alignment) {
        FailingAllocator *self = (FailingAllocator *)ctx;
        if (self->alloc_index == self->fail_index) {
            if (!self->has_induced_failure) self->has_induced_failure = true;
            return nullptr;
        }
        uint8_t *result = self->internal_allocator.vtable->alloc(self->internal_allocator.ptr, len, alignment);
        if (!result) return nullptr;
        self->allocated_bytes += len;
        self->allocations += 1;
        self->alloc_index += 1;
        return result;
    }
    static void freeFn(void *ctx, uint8_t *memory, size_t len, size_t alignment) {
        FailingAllocator *self = (FailingAllocator *)ctx;
        self->internal_allocator.vtable->free(self->internal_allocator.ptr, memory, len, alignment);
        self->deallocations += 1;
        self->freed_bytes += len;
    }

    Allocator allocator() {
        static const Allocator::VTable vt = {allocFn, freeFn};
        Allocator a = {this, &vt};
        return a;
    }
};

/* std.testing.allocator stand-in: the C heap with a live-allocation count
 * so tests can check for leaks. */
struct TestingAllocatorState {
    size_t live;
};
inline TestingAllocatorState &testing_state() {
    static TestingAllocatorState s = {0};
    return s;
}
namespace detail {
inline uint8_t *testAlloc(void *, size_t len, size_t alignment) {
    uint8_t *p = cAlloc(nullptr, len, alignment);
    if (p) testing_state().live += 1;
    return p;
}
inline void testFree(void *, uint8_t *memory, size_t, size_t) {
    testing_state().live -= 1;
    cFree(nullptr, memory, 0, 0);
}
} /* namespace detail */

inline Allocator testing_allocator() {
    static const Allocator::VTable vt = {detail::testAlloc, detail::testFree};
    Allocator a = {nullptr, &vt};
    return a;
}

} /* namespace zigstd */
} /* namespace wisp */

#endif /* WISP_ZIGSTD_ALLOCATOR_HPP */
