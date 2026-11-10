/* x86 bridge for the nonstandard live-register contract at Driver 0x8bbe30.
 * The caller immediately reuses ECX for 0x8bc6e0 without reloading it.
 * C++ helpers may clobber volatile GPRs, EFLAGS, x87, SSE and MXCSR.
 * Keep their effects separate from the original function's exact return state.
 * EBP anchors our stack during the native call: verified original leaf neither
 * reads nor writes EBP (nor calls another function). Restore caller EBP on exit.
 */
#ifdef _WIN32
#define SYMBOL_(x) _##x
#define SYMBOL(x) SYMBOL_(x)
#else
#define SYMBOL(x) x
#endif
.text
.p2align 4
.globl SYMBOL(nitroHeatHook)
#ifndef _WIN32
.type SYMBOL(nitroHeatHook),@function
#endif
SYMBOL(nitroHeatHook):
    /* At entry: ECX=self, [ESP+4]=output; original cleans one stack argument. */
    pushfl
    pushal
    movl %esp,%ebp
    subl $544,%esp
    andl $-16,%esp
    /* 0..511 FXSAVE, 516..531 NitroContext, 536 helper MXCSR. */
    fxsave (%esp)
    fninit
    movl $0x1f80,536(%esp)
    ldmxcsr 536(%esp)
    cld
    leal 516(%esp),%edx
    subl $8,%esp
    pushl %edx
    pushl 24(%ebp)
    call SYMBOL(nitroHeatBefore)
    addl $16,%esp
    fxrstor (%esp)
    /* All native inputs except the unused EBP match the intercepted entry. */
    movl 28(%ebp),%eax
    movl 20(%ebp),%edx
    movl 24(%ebp),%ecx
    pushl 32(%ebp)
    popfl
    pushl 40(%ebp)
    call *SYMBOL(originalNitroHeat)
    /* Save actual native results BEFORE calling any post-processing code. */
    pushfl
    pushal
    movl %esp,%eax
    leal 36(%eax),%edi
    subl $528,%esp
    andl $-16,%esp
    movl %eax,512(%esp)
    fxsave (%esp)
    fninit
    movl $0x1f80,516(%esp)
    ldmxcsr 516(%esp)
    cld
    leal 516(%edi),%edx
    subl $8,%esp
    pushl %edx
    pushl 24(%ebp)
    call SYMBOL(nitroHeatAfter)
    addl $16,%esp
    fxrstor (%esp)
    movl 512(%esp),%esp
    popal
    popfl
    /* LEA/MOV/RET leave native EFLAGS, ECX, EDX, EAX and FP/SIMD intact. */
    leal 36(%ebp),%esp
    movl 8(%ebp),%ebp
    ret $4
#ifndef _WIN32
.size SYMBOL(nitroHeatHook),.-SYMBOL(nitroHeatHook)
.section .note.GNU-stack,"",@progbits
#endif
