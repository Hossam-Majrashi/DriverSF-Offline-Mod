/* Driver 0x7a917d interior bridge: change only saved XMM1.low32 (speed
 * threshold), preserving native GPRs, EFLAGS, stack, x87, MXCSR and all other
 * SIMD lanes. Jump to MinHook's relocated original load/continuation; no RET
 * emulation, force, velocity, input or native-object write. */
#ifdef _WIN32
#define SYMBOL_(x) _##x
#define SYMBOL(x) SYMBOL_(x)
#else
#define SYMBOL(x) x
#endif
.text
.p2align 4
.globl SYMBOL(boostCeilingHook)
#ifndef _WIN32
.type SYMBOL(boostCeilingHook),@function
#endif
SYMBOL(boostCeilingHook):
    pushfl
    pushal
    movl %esp,%ebp
    subl $528,%esp
    andl $-16,%esp
    fxsave (%esp)
    fninit
    movl $0x1f80,512(%esp)
    ldmxcsr 512(%esp)
    cld
    movl %esp,%eax
    subl $8,%esp
    pushl %eax
    pushl 0(%ebp) /* native EDI = ability input object */
    call SYMBOL(boostCeilingSnapshot)
    addl $16,%esp
    fxrstor (%esp)
    movl %ebp,%esp
    popal
    popfl
    jmp *SYMBOL(originalBoostCapTarget)
#ifndef _WIN32
.size SYMBOL(boostCeilingHook),.-SYMBOL(boostCeilingHook)
.section .note.GNU-stack,"",@progbits
#endif
