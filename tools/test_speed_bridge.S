/* Offline 32-bit probe for the real production interior bridge. */
.data
.p2align 4
probe_caller_fx: .space 512
probe_output: .long 0
.section .rodata
.p2align 4
probe_vector: .long 0x3f800000,0x40000000,0x40400000,0x40800000
probe_mxcsr: .long 0x3f80
.text
.globl speed_probe
.type speed_probe,@function
speed_probe:
    pushfl
    pushal
    fxsave probe_caller_fx
    movl 48(%esp),%eax
    movl %eax,probe_output
    fninit
    fldpi
    ldmxcsr probe_mxcsr
    movdqu probe_vector,%xmm0
    movdqu probe_vector,%xmm1
    movdqu probe_vector,%xmm2
    movdqu probe_vector,%xmm3
    movdqu probe_vector,%xmm4
    movdqu probe_vector,%xmm5
    movdqu probe_vector,%xmm6
    movdqu probe_vector,%xmm7
    movss 44(%esp),%xmm1
    movl 40(%esp),%edi
    movl $0x13579bdf,%esi
    movl $0x2468ace0,%ebp
    movl $0x23456789,%ebx
    movl $0x3456789a,%edx
    movl $0x456789ab,%ecx
    movl $0x56789abc,%eax
    std
    stc
    pushfl
    pushal
    movl %esp,%ebp
    movl probe_output,%edi
    fxsave 48(%edi)
    movl %esp,%esi
    movl $9,%ecx
    cld
    rep movsl
    movl %ebp,%esp
    popal
    popfl
    call boostCeilingHook
    fxrstor probe_caller_fx
    popal
    popfl
    ret
.size speed_probe,.-speed_probe
.globl speed_probe_continuation
.type speed_probe_continuation,@function
speed_probe_continuation:
    pushfl
    pushal
    movl %esp,%ebp
    movl probe_output,%edi
    addl $560,%edi
    fxsave 48(%edi)
    movl %esp,%esi
    movl $9,%ecx
    cld
    rep movsl
    movl %ebp,%esp
    popal
    popfl
    ret
.size speed_probe_continuation,.-speed_probe_continuation
.section .note.GNU-stack,"",@progbits
