/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "context.h"

#define fault_printf(...)

	#if HOST_OS == OS_DARWIN
		#define _XOPEN_SOURCE 1
		#define __USE_GNU 1
	#endif

  #if !defined(TARGET_NO_EXCEPTIONS)
    #include <ucontext.h>
  #endif


//////

#define UCTX(p) (((ucontext_t *)(segfault_ctx)) p)

#if HOST_OS == OS_DARWIN
	#define UCTX_type ucontext_t
	#define UCTX_data (UCTX())
#else
	#define UCTX_type ucontext_t
	#define UCTX_data (UCTX())
#endif

template <typename Ta, typename Tb>
void bicopy(Ta& rei, Tb& seg, bool to_segfault) {
	if (to_segfault) {
		seg = rei;
	}
	else {
		rei = seg;
	}
}
#if !defined(TARGET_NO_EXCEPTIONS)
	static thread_local UCTX_type segfault_copy;
#endif

void segfault_store(void* segfault_ctx) {
#if !defined(TARGET_NO_EXCEPTIONS)
	memcpy(&segfault_copy, UCTX_data, sizeof(segfault_copy));

	fault_printf("sgctx stored, %d\n", sizeof(segfault_copy));
#endif
}

void segfault_load(void* segfault_ctx) {
#if !defined(TARGET_NO_EXCEPTIONS)
	memcpy(UCTX_data, &segfault_copy, sizeof(*UCTX_data));

	fault_printf("sgctx loaded, %d\n", sizeof(*UCTX_data));
#endif
}

#define MCTX(p) (((UCTX_type*)segfault_ctx)->uc_mcontext p)

void context_segfault_bicopy(rei_host_context_t* reictx, void* segfault_ctx, bool to_segfault) {

#if !defined(TARGET_NO_EXCEPTIONS)
#if HOST_CPU == CPU_ARM64
	bicopy(reictx->pc, MCTX(->__ss.__pc), to_segfault);
#elif HOST_CPU == CPU_X64
	#if HOST_OS == OS_LINUX
		fault_printf("pc set: %d, value: %p %p\n", to_segfault, reictx->pc, MCTX(.gregs[REG_RIP]));
		bicopy(reictx->pc, MCTX(.gregs[REG_RIP]), to_segfault);
		fault_printf("pc set: %d, value: %p %p\n", to_segfault, reictx->pc, MCTX(.gregs[REG_RIP]));
    #elif HOST_OS == OS_DARWIN
        bicopy(reictx->pc, MCTX(->__ss.__rip), to_segfault);
    #else
	    #error HOST_OS
	#endif
#elif HOST_CPU == CPU_GENERIC
    //nothing!
#else
	#error Unsupported HOST_CPU
#endif
	#endif
	
}


void segfault_set_pc(void* segfault_ctx, unat new_pc, unat* old_pc)
{
#if !defined(TARGET_NO_EXCEPTIONS)
	rei_host_context_t ctx;

	context_segfault_bicopy(&ctx, UCTX_data, false);
	fault_printf("segfault_set_pc: old pc: %lx\n", ctx.pc);

	*old_pc = ctx.pc;

	ctx.pc = new_pc;
	
	context_segfault_bicopy(&ctx, UCTX_data, true);
	fault_printf("segfault_set_pc: new pc: %lx\n", ctx.pc);
#endif
}


unat segfault_get_pc(void* segfault_ctx)
{
#if !defined(TARGET_NO_EXCEPTIONS) && HOST_CPU != CPU_GENERIC
	rei_host_context_t ctx;

	context_segfault_bicopy(&ctx, UCTX_data, false);
	return ctx.pc;
#else
	return 0;
#endif
}

void context_from_segfault(rei_host_context_t* reictx) {
#if !defined(TARGET_NO_EXCEPTIONS)
	fault_printf("context from segfault pc: %p, %p\n",reictx->pc, segfault_copy.gregs[REG_RIP]);
	context_segfault_bicopy(reictx, &segfault_copy, false);
	fault_printf("context from segfault pc: %p, %p\n",reictx->pc, segfault_copy.gregs[REG_RIP]);
#endif
}

void context_to_segfault(rei_host_context_t* reictx) {
#if !defined(TARGET_NO_EXCEPTIONS)
	fault_printf("context to segfault pc: %p, %p\n",reictx->pc, segfault_copy.gregs[REG_RIP]);
	context_segfault_bicopy(reictx, &segfault_copy, true);
	fault_printf("context to segfault pc: %p, %p\n",reictx->pc, segfault_copy.gregs[REG_RIP]);
#endif
}
