#pragma once

// inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }

#define CACHELINE_SIZE 64