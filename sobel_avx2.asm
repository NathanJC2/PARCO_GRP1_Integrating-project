default rel
bits 64

section .rodata
align 32
const_half: dq 0.5, 0.5, 0.5, 0.5

section .text
global sobel_avx2

sobel_avx2:
    ; Standard prologue & non-volatile register preservation
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

%ifidn __OUTPUT_FORMAT__, win64
    ; Windows x64 ABI: RCX = input, RDX = output, R8 = width, R9 = height
    sub rsp, 168
    vmovups [rsp + 0],   xmm6
    vmovups [rsp + 16],  xmm7
    vmovups [rsp + 32],  xmm8
    vmovups [rsp + 48],  xmm9
    vmovups [rsp + 64],  xmm10
    vmovups [rsp + 80],  xmm11
    vmovups [rsp + 96],  xmm12
    vmovups [rsp + 112], xmm13
    vmovups [rsp + 128], xmm14
    vmovups [rsp + 144], xmm15

    mov r12, rcx        ; r12 = input
    mov r13, rdx        ; r13 = output
    mov r14, r8         ; r14 = width
    mov r15, r9         ; r15 = height
%else
    ; System V AMD64 ABI (Linux/macOS): RDI = input, RSI = output, RDX = width, RCX = height
    mov r12, rdi        ; r12 = input
    mov r13, rsi        ; r13 = output
    mov r14, rdx        ; r14 = width
    mov r15, rcx        ; r15 = height
%endif

    ; 1. Validate arguments: input != NULL, output != NULL, width > 0, height > 0, input != output
    test r12, r12
    jz .invalid_input
    test r13, r13
    jz .invalid_input
    test r14, r14
    jz .invalid_input
    test r15, r15
    jz .invalid_input
    cmp r12, r13
    je .invalid_input

    ; Check overflow: width > SIZE_MAX / height -> rax = width * height
    mov rax, r14
    mul r15             ; rdx:rax = width * height
    jo .invalid_input
    mov rcx, rax        ; rcx = pixel_count

    ; 2. Zero out the entire output buffer (handles all borders & < 3x3 images)
    mov rdi, r13
    xor eax, eax
    rep stosb

    ; If width < 3 or height < 3, return 1 immediately
    cmp r14, 3
    jb .success
    cmp r15, 3
    jb .success

    ; Setup row pointers:
    ; rsi = &input[0]         (top row, y - 1)
    ; rdi = &input[width]     (middle row, y)
    ; rbx = &input[2 * width] (bottom row, y + 1)
    ; rdx = &output[width]    (output middle row, y)
    mov rsi, r12
    lea rdi, [r12 + r14]
    lea rbx, [rdi + r14]
    lea rdx, [r13 + r14]

    ; Interior row count = height - 2
    mov r8, r15
    sub r8, 2

    ; Interior column end limit = width - 1
    mov r9, r14
    dec r9

    ; Vectorized column limit = width - 17
    xor r10, r10
    cmp r14, 18
    jb .row_loop
    mov r10, r14
    sub r10, 17

.row_loop:
    mov rcx, 1          ; x = 1

    ; Skip vector loop if width < 18
    cmp r14, 18
    jb .scalar_cleanup

.vec_loop:
    cmp rcx, r10
    ja .scalar_cleanup

    ; Load 16 uint8_t neighbors and zero-extend to 16 x int16_t in YMM registers
    vpmovzxbw ymm0, [rsi + rcx - 1]     ; top_left
    vpmovzxbw ymm1, [rsi + rcx]         ; top_mid
    vpmovzxbw ymm2, [rsi + rcx + 1]     ; top_right

    vpmovzxbw ymm3, [rdi + rcx - 1]     ; mid_left
    vpmovzxbw ymm4, [rdi + rcx + 1]     ; mid_right

    vpmovzxbw ymm5, [rbx + rcx - 1]     ; bot_left
    vpmovzxbw ymm6, [rbx + rcx]         ; bot_mid
    vpmovzxbw ymm7, [rbx + rcx + 1]     ; bot_right

    ; TODO: Compute Gx and Gy gradients in 16-bit signed integers (ymm8, ymm9)
    ; TODO: Widen Gx/Gy to 32-bit integers (vpmovsxwd) and compute Gx^2 + Gy^2
    ; TODO: Convert squared magnitude to double, compute vsqrtpd + 0.5,
    ;       saturate to [0..255], pack to uint8, and store 16 pixels to [rdx + rcx]

    add rcx, 16
    jmp .vec_loop

.scalar_cleanup:
    ; TODO: Implement scalar fallback loop for remaining interior pixels (rcx < r9)
    ;       when (width - 2) is not divisible by 16.

.next_row:
    add rsi, r14
    add rdi, r14
    add rbx, r14
    add rdx, r14
    dec r8
    jnz .row_loop

.success:
    mov eax, 1
    jmp .epilogue

.invalid_input:
    xor eax, eax

.epilogue:
    vzeroupper

%ifidn __OUTPUT_FORMAT__, win64
    vmovups xmm6,  [rsp + 0]
    vmovups xmm7,  [rsp + 16]
    vmovups xmm8,  [rsp + 32]
    vmovups xmm9,  [rsp + 48]
    vmovups xmm10, [rsp + 64]
    vmovups xmm11, [rsp + 80]
    vmovups xmm12, [rsp + 96]
    vmovups xmm13, [rsp + 112]
    vmovups xmm14, [rsp + 128]
    vmovups xmm15, [rsp + 144]
    add rsp, 168
%endif

    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
