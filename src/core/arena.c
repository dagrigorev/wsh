/*
 * arena.c — Region-based allocator implementation.
 *
 * The arena is a singly-linked list of blocks (chunks).  Most allocations hit
 * the current chunk; overflow allocates a new chunk sized as max(needed, 2x
 * previous).  arena_reset() resets to the first chunk; blocks after the first
 * are freed to avoid unbounded memory growth across command cycles.
 */
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "arena.h"

/* Each backing block in the chain. */
typedef struct Block {
    char        *data;
    size_t       cap;
    size_t       used;
    struct Block *next;
} Block;

struct Arena {
    Block  *head;    /* current (latest) block */
    Block  *first;   /* very first block (kept across reset) */
    size_t  initial_cap;
};

/* Pointer-size alignment helper. */
static size_t align_up(size_t n) {
    const size_t a = sizeof(void *);
    return (n + a - 1) & ~(a - 1);
}

static Block *block_new(size_t cap) {
    Block *b = (Block *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Block));
    if (!b) return NULL;
    b->data = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
    if (!b->data) { HeapFree(GetProcessHeap(), 0, b); return NULL; }
    b->cap  = cap;
    b->used = 0;
    b->next = NULL;
    return b;
}

Arena *arena_create(size_t initial_capacity) {
    if (initial_capacity < 4096) initial_capacity = 4096;
    Arena *a = (Arena *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Arena));
    if (!a) return NULL;
    a->initial_cap = initial_capacity;
    a->head = a->first = block_new(initial_capacity);
    if (!a->head) { HeapFree(GetProcessHeap(), 0, a); return NULL; }
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    if (size == 0) size = 1;
    size = align_up(size);

    /* Fast path: fits in current block. */
    if (a->head->used + size <= a->head->cap) {
        void *p = a->head->data + a->head->used;
        a->head->used += size;
        return p;
    }

    /* Need a new block; size it generously to amortise allocations. */
    size_t new_cap = a->head->cap * 2;
    if (new_cap < size) new_cap = size;

    Block *b = block_new(new_cap);
    if (!b) {
        /* OOM is unrecoverable in a terminal shell. */
        fprintf(stderr, "wsh: arena OOM (%zu bytes)\n", size);
        abort();
    }

    /* Prepend new block so a->head is always the active one. */
    b->next  = a->head;
    a->head  = b;
    b->used  = size;
    return b->data; /* zero-initialised by HeapAlloc HEAP_ZERO_MEMORY */
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char  *p = (char *)arena_alloc(a, n);
    memcpy(p, s, n);
    return p;
}

void arena_reset(Arena *a) {
    /* Free all blocks except the first (which we recycle). */
    Block *b = a->head;
    while (b && b != a->first) {
        Block *next = b->next;
        HeapFree(GetProcessHeap(), 0, b->data);
        HeapFree(GetProcessHeap(), 0, b);
        b = next;
    }
    a->head       = a->first;
    a->first->used = 0;
    /* Zero the reused block for determinism during debugging. */
    memset(a->first->data, 0, a->first->cap);
}

void arena_destroy(Arena *a) {
    if (!a) return;
    Block *b = a->head;
    while (b) {
        Block *next = b->next;
        HeapFree(GetProcessHeap(), 0, b->data);
        HeapFree(GetProcessHeap(), 0, b);
        b = next;
    }
    HeapFree(GetProcessHeap(), 0, a);
}
