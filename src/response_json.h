// Bound retained JSON separately from the number of bytes read from Bambuddy.
#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstddef>

class ResponseAllocator : public ArduinoJson::Allocator {
  struct alignas(std::max_align_t) Block { size_t size; };
  size_t used = 0;
  static constexpr size_t LIMIT = 40 * 1024;
public:
  void *allocate(size_t size) override { return reallocate(nullptr, size); }
  void deallocate(void *ptr) override {
    if (!ptr) return;
    Block *block = static_cast<Block *>(ptr) - 1;
    used -= sizeof(Block) + block->size;
    free(block);
  }
  void *reallocate(void *ptr, size_t size) override {
    Block *old = ptr ? static_cast<Block *>(ptr) - 1 : nullptr;
    const size_t retained = used - (old ? sizeof(Block) + old->size : 0);
    if (LIMIT - retained < sizeof(Block) || size > LIMIT - retained - sizeof(Block)) return nullptr;
    Block *block = static_cast<Block *>(realloc(old, sizeof(Block) + size));
    if (!block) return nullptr;
    block->size = size;
    used = retained + sizeof(Block) + size;
    return block + 1;
  }
};

class ResponseDocument : private ResponseAllocator, public JsonDocument {
public:
  ResponseDocument() : JsonDocument(static_cast<ResponseAllocator *>(this)) {}
  ResponseDocument(const ResponseDocument &) = delete;
};
