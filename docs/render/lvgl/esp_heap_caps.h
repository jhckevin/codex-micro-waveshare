#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
inline void* heap_caps_malloc(size_t n, unsigned) { return std::malloc(n); }
inline void* heap_caps_calloc(size_t n,size_t s,unsigned) { return std::calloc(n,s); }
inline void heap_caps_free(void* p) { std::free(p); }
