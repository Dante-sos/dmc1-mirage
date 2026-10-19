.intel_syntax noprefix
.text
.global stub_sample
stub_sample:
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rbx, rsp
    and rsp, -16
    sub rsp, 0x80
    movaps [rsp+0x20], xmm0
    movaps [rsp+0x30], xmm1
    movaps [rsp+0x40], xmm2
    movaps [rsp+0x50], xmm3
    movaps [rsp+0x60], xmm4
    movaps [rsp+0x70], xmm5
    mov rcx, 0x1122334455667788
    call rcx
    movaps xmm0, [rsp+0x20]
    movaps xmm1, [rsp+0x30]
    movaps xmm2, [rsp+0x40]
    movaps xmm3, [rsp+0x50]
    movaps xmm4, [rsp+0x60]
    movaps xmm5, [rsp+0x70]
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
    ret
