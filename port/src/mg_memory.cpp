#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mg_memory.hpp"

namespace {

constexpr std::uintptr_t kQuadwordBytes = sizeof(u_long128);

// A PS2 header is one quadword. A host header holds two 8-byte pointers, 24 bytes, so it takes two
// quadwords. Every size and gap below counts in quadwords, header included.
constexpr int kHeaderQuadwords = static_cast<int>((sizeof(mgMEMORY_BLOCK) + kQuadwordBytes - 1) / kQuadwordBytes);

std::uintptr_t AddressOf(const void *pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

u_long128 *QuadwordsFrom(const void *base, std::ptrdiff_t quadwords) {
    return reinterpret_cast<u_long128 *>(static_cast<unsigned char *>(const_cast<void *>(base)) +
                                         quadwords * static_cast<std::ptrdiff_t>(kQuadwordBytes));
}

std::ptrdiff_t QuadwordsBetween(const void *from, const void *to) {
    return (static_cast<const unsigned char *>(to) - static_cast<const unsigned char *>(from)) /
           static_cast<std::ptrdiff_t>(kQuadwordBytes);
}

mgMEMORY_BLOCK *BlockAt(const void *base, std::ptrdiff_t quadwords) {
    return reinterpret_cast<mgMEMORY_BLOCK *>(QuadwordsFrom(base, quadwords));
}

u_long128 *AllocQuadwords(mgCMemory &memory, int size) {
    if (memory.lock != 0 || size <= 0) {
        return nullptr;
    }

    // Summed in 64 bits so a huge request fails the capacity check instead of wrapping past it.
    const long long new_used = static_cast<long long>(memory.stack_used) + size;

    if (new_used >= memory.stack_size) {
        std::printf("stack over %lld/%d at %.16s\n", new_used, memory.stack_size, memory.name);
        return nullptr;
    }

    u_long128 *data = QuadwordsFrom(memory.stack, memory.stack_used);
    memory.stack_used = static_cast<int>(new_used);
    return data;
}

void AlignStack(mgCMemory &memory) {
    if (memory.lock) {
        return;
    }

    // Quadword-aligned on both hosts, so the misalignment is a multiple of 16 and the step is exact.
    const std::uintptr_t misalign = AddressOf(QuadwordsFrom(memory.stack, memory.stack_used)) & 0x3F;

    if (misalign != 0) {
        memory.stack_used += static_cast<int>(64 - misalign) / 16;
    }

    if (memory.stack_used >= memory.stack_size) {
        memory.stack_used = memory.stack_size;
    }
}

} // namespace

PORT_OVERRIDE void mgCMemory::Init() {
    heap_size = 0;
    heap = nullptr;
    heap_top = nullptr;
    name[0] = '\0';
    stack = nullptr;
    stack_used = 0;
    stack_size = 0;
    stack_block = nullptr;
    lock = 0;
}

PORT_OVERRIDE void mgCMemory::SetHeapMem(u_long128 *buffer, int size) {
    // Signed on purpose: an unsigned compare would take a negative size as huge and map the terminator
    // past the end of the buffer.
    if (buffer == nullptr || size < 0x10) {
        Init();
        return;
    }

    heap = buffer;
    heap_size = static_cast<u_int>(size);
    heap_top = reinterpret_cast<mgMEMORY_BLOCK *>(buffer);
    heap_top->data = nullptr;
    heap_top->size = kHeaderQuadwords;
    heap_top->next = BlockAt(buffer, static_cast<std::ptrdiff_t>(heap_size) - kHeaderQuadwords);
    heap_top->next->data = nullptr;
    heap_top->next->size = 0;
    heap_top->next->next = nullptr;
}

PORT_OVERRIDE void mgCMemory::ClearHeapMem() {
    u_long128 *buffer = heap;
    const int size = static_cast<int>(heap_size);

    Init();
    SetHeapMem(buffer, size);
}

PORT_OVERRIDE void mgCMemory::Free(u_long128 *data) {
    if (data == nullptr) {
        return;
    }

    mgMEMORY_BLOCK *prev = nullptr;
    mgMEMORY_BLOCK *found = nullptr;

    for (mgMEMORY_BLOCK *block = heap_top; block->next != nullptr; block = block->next) {
        if (block->data == data) {
            found = block;
            break;
        }

        prev = block;
    }

    if (found == nullptr) {
        // Retail halts here. A host process that spins would hang the test run, so it aborts instead.
        std::fprintf(stderr, "Illegal Free Memory %p\n", static_cast<void *>(data));
        std::abort();
    }

    prev->next = found->next;
}

PORT_OVERRIDE u_long128 *mgCMemory::StartStackMode(int mode, int size) {
    stack_block = nullptr;
    mgMEMORY_BLOCK *block = heap_top;

    if (block == nullptr) {
        return nullptr;
    }

    mgMEMORY_BLOCK *chosen = nullptr;
    mgMEMORY_BLOCK *chosen_previous = nullptr;
    std::ptrdiff_t  chosen_count = 0;
    std::ptrdiff_t  best_count = 0;

    for (mgMEMORY_BLOCK *next; (next = block->next) != nullptr; block = next) {
        mgMEMORY_BLOCK *gap = BlockAt(block, block->size);
        const std::ptrdiff_t count = QuadwordsBetween(gap, next);

        if (mode == 1 && count > kHeaderQuadwords) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            break;
        }

        // A host header does not fit a one-quadword gap, which retail's one-quadword header does.
        if (mode == 2 && count >= kHeaderQuadwords && best_count < count) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            best_count = count;
        }

        if (mode == 3 && size + kHeaderQuadwords < count) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            break;
        }
    }

    if (chosen == nullptr) {
        return nullptr;
    }

    // Sized from the chosen gap. Retail sizes it from the last gap examined, so mode 2 can run the
    // stack region past the end of the gap it claimed and over the next header.
    stack_block = chosen;
    stack_block->next = chosen_previous->next;
    chosen_previous->next = stack_block;
    stack_block->size = kHeaderQuadwords;
    stack_block->data = QuadwordsFrom(stack_block, kHeaderQuadwords);
    stack = stack_block->data;
    stack_size = static_cast<int>(chosen_count - kHeaderQuadwords);
    return stack_block->data;
}

PORT_OVERRIDE void mgCMemory::EndStackMode() {
    mgMEMORY_BLOCK *block = stack_block;

    if (block != nullptr) {
        block->size += stack_used;
        stack_block = nullptr;
        stack = nullptr;
        stack_size = 0;
        stack_used = 0;
    }
}

PORT_OVERRIDE u_long128 *mgCMemory::stAlloc64(int size) {
    stAlign64();
    return stAlloc(size);
}

PORT_OVERRIDE u_long128 *mgCMemory::stAllocTest(int size) {
    if (lock) {
        return nullptr;
    }

    const long long new_used = static_cast<long long>(stack_used) + size;

    if (new_used >= stack_size) {
        std::printf("stack over %lld/%d at %.16s\n", new_used, stack_size, name);
        return nullptr;
    }

    return QuadwordsFrom(stack, stack_used);
}

PORT_OVERRIDE u_long128 *mgCMemory::stAlloc(int size) {
    return AllocQuadwords(*this, size);
}

PORT_OVERRIDE u_long128 *mgCMemory::Alloc(int size) {
    return AllocQuadwords(*this, size);
}

PORT_OVERRIDE void mgCMemory::stAlign64() {
    AlignStack(*this);
}

PORT_OVERRIDE void mgCMemory::Align64() {
    AlignStack(*this);
}

PORT_OVERRIDE void mgCMemory::stSetBuffer(u_long128 *buffer, int size) {
    stack = buffer;
    stack_used = 0;
    stack_size = size;
}
