/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once

#include <atomic>
#include <stdint.h>
#include <type_traits>

/*
	A u32 that is read and written atomically, for the places where a std::atomic<u32> cannot stand: as a member
	of an anonymous struct (g++ takes no member with a constructor there), in memory that nothing constructs, and
	in an object that is cleared with memset and saved with memcpy. Sh4Context::CpuRunning (hw/sh4/sh4_if.h) is
	all three.

	It has the size, the alignment and the representation of a u32 and no constructor: it is whatever its memory
	holds, and code that knows where it is loads and stores its 32 bits (the recompilers do, from generated
	code: a load of 32 bits is a relaxed load of it).

	Its operations are those of a std::atomic<u32>, with the defaults of one: load and store take a
	std::memory_order and are sequentially consistent without one, and so are the conversion and the
	assignment. They are made through a std::atomic_ref<u32>, which is the standard's atomic access to an
	object that is no std::atomic; a library that has none (libc++ before 19) gets the compiler's builtins,
	which a std::atomic_ref is made of.

	Whoever clears or copies the object that holds one as bytes does so while no other thread uses it.
*/
struct AtomicU32
{
	uint32_t load(std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
#if defined(__cpp_lib_atomic_ref) && !defined(ATOMIC_U32_BUILTINS)
		return std::atomic_ref<uint32_t>(const_cast<uint32_t&>(word)).load(order);
#elif defined(__GNUC__)
		return __atomic_load_n(&word, (int)order);
#else
		#error AtomicU32: no std::atomic_ref and no atomic builtins
#endif
	}

	void store(uint32_t value, std::memory_order order = std::memory_order_seq_cst) noexcept
	{
#if defined(__cpp_lib_atomic_ref) && !defined(ATOMIC_U32_BUILTINS)
		std::atomic_ref<uint32_t>(word).store(value, order);
#elif defined(__GNUC__)
		__atomic_store_n(&word, value, (int)order);
#else
		#error AtomicU32: no std::atomic_ref and no atomic builtins
#endif
	}

	operator uint32_t() const noexcept { return load(); }

	uint32_t operator=(uint32_t value) noexcept { store(value); return value; }

private:
	uint32_t word;
};

static_assert(sizeof(AtomicU32) == sizeof(uint32_t) && alignof(AtomicU32) == alignof(uint32_t), "an AtomicU32 is its 32 bits");
static_assert(std::is_trivial<AtomicU32>::value && std::is_standard_layout<AtomicU32>::value, "an AtomicU32 is whatever its memory holds");
static_assert(ATOMIC_INT_LOCK_FREE == 2, "an AtomicU32 has no lock: generated code reads it with one load");
