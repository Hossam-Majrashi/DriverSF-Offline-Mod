/* Isolated 32-bit ABI probe. Test code only; never linked into the mod DLL. */
.text
.globl probe_nitro_abi
.type probe_nitro_abi,@function
probe_nitro_abi:
    pushl %ebp
    movl %esp,%ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp),%eax
    movl 12(%ebp),%ecx
    movl 16(%ebp),%edx
    movl %esp,%ebx
    movl $0x13579bdf,%esi
    movl $0x2468ace0,%edi
    pushl %edx
    call *%eax
    pushfl
    pushal
    movl 20(%ebp),%edi
    movl 24(%esp),%eax
    movl %eax,0(%edi)
    movl 20(%esp),%eax
    movl %eax,4(%edi)
    movl 28(%esp),%eax
    movl %eax,8(%edi)
    movl 32(%esp),%eax
    movl %eax,12(%edi)
    movl 8(%esp),%eax
    movl %eax,16(%edi)
    leal 36(%esp),%eax
    movl %eax,20(%edi)
    movl 16(%esp),%eax
    movl %eax,24(%edi)
    movl 4(%esp),%eax
    movl %eax,28(%edi)
    movl 0(%esp),%eax
    movl %eax,32(%edi)
    movl 16(%esp),%eax
    movl %eax,36(%edi)
    fxsave 48(%edi)
    popal
    popfl
    fninit
    ldmxcsr abi_default_mxcsr
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
.size probe_nitro_abi,.-probe_nitro_abi

.globl nitro_abi_native
.type nitro_abi_native,@function
nitro_abi_native:
    /* Mimic the real leaf's ECX-preserving, ret-4 contract and make every
       important native return channel recognizable after C++ helpers run. */
    movl 4(%esp),%edx
    movl $0x12345678,%eax
    movups abi_xmm_pattern,%xmm0
    fld1
    ldmxcsr abi_native_mxcsr
    stc
    ret $4
.size nitro_abi_native,.-nitro_abi_native
.section .rodata
.p2align 4
abi_xmm_pattern: .long 0x3f800000,0x40000000,0x40400000,0x40800000
abi_native_mxcsr: .long 0x3f80
abi_default_mxcsr: .long 0x1f80
.section .note.GNU-stack,"",@progbits
