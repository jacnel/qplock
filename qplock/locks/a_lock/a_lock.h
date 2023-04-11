#pragma once

#include <assert.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "rome/rdma/memory_pool/memory_pool.h"
#include "../../util.h"

namespace X {

using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;

static constexpr uint32_t kInitBudget = 5;

#define LOCAL_VICTIM  0
#define REMOTE_VICTIM 1

// TODO: Use for bitset in server context
static constexpr bool FREE_DESC = 0;
static constexpr bool IN_USE_DESC = 1;

struct alignas(64) RdmaDescriptor {
    int8_t budget{-1};
    uint8_t pad1[16 - sizeof(budget)];
    bool locked{0}; //track when locked
    uint8_t pad2[16 - sizeof(locked)];
    remote_ptr<RdmaDescriptor> next{0};
    uint8_t pad3[32 - sizeof(next)];
};
static_assert(alignof(RdmaDescriptor) == CACHELINE_SIZE);
static_assert(sizeof(RdmaDescriptor) == CACHELINE_SIZE);

struct alignas(64) LocalDescriptor {
    int8_t budget{-1}; //budget == -1 indicates its locked, unlocked and passed off when it can proceed to critical section
    uint8_t pad1[16 - sizeof(budget)];
    bool locked{0}; //track when locked
    uint8_t pad2[16 - sizeof(locked)];
    LocalDescriptor* next{nullptr};
    uint8_t pad3[32 - sizeof(next)];
};
static_assert(alignof(LocalDescriptor) == CACHELINE_SIZE);
static_assert(sizeof(LocalDescriptor) == CACHELINE_SIZE);

// ! SHOULD THESE BE PADDED SUCH THAT THE POINTERS DO NOT OVERLAP CACHE LINES TO AVOID USING STALE CACHED VALUES?
struct alignas(64) ALock {
    // pointer to the pointer of the remote tail
    remote_ptr<RdmaDescriptor> r_tail;
    // pad so local tail starts at addr+16
    uint8_t pad1[16 - sizeof(r_tail)]; 
    // pointer to the local tail
    remote_ptr<LocalDescriptor> l_tail; 
    // pad so victim starts at addr+32
    uint8_t pad2[16 - sizeof(l_tail)]; 
    // node id of the victim
    uint64_t victim; 
    // pad so locked starts at addr+16
    uint8_t pad3[16 - sizeof(victim)]; 
    // track if handle has been locked
    bool locked{0};
    // pad to fill cacheline
    uint8_t pad4[16 - sizeof(locked)];
};

static_assert(alignof(ALock) == CACHELINE_SIZE);
static_assert(sizeof(ALock) == CACHELINE_SIZE);

} //namespace X