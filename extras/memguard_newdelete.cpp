/*
 * Optional bridge: route every C++ `new`/`delete` in FTLCheck through
 * the MemGuard allocator (../../memguard — project 2).
 *
 * C++ lets a program replace the global allocation functions; the
 * linker then sends ALL new/delete calls (including the standard
 * library's) here. Built by `make memguard` / build-memguard.bat.
 */
#include "../../memguard/src/memguard.h"

#include <new>

void* operator new(std::size_t size) {
    void* p = mg_malloc_at(size ? size : 1, "operator new", 0);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return mg_malloc_at(size ? size : 1, "operator new(nothrow)", 0);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return mg_malloc_at(size ? size : 1, "operator new[](nothrow)", 0);
}

void operator delete(void* p) noexcept {
    if (p) mg_free_at(p, "operator delete", 0);
}

void operator delete[](void* p) noexcept {
    operator delete(p);
}

void operator delete(void* p, std::size_t) noexcept   { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept   { operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { operator delete(p); }
