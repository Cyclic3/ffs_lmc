[BITS 32]
; halt
ret
nop

;load
mov eax, [0x7fffffff] ; load
nop

; store
mov [0x7fffffff], eax
nop

; input
push ebp ; save our base pointer
mov ebp, esp ; prep the stack frame
call 0x7fffffff ; Luckily for us, cdecl stores the result in eax
mov esp, ebp ; Move the stack pointer back down
pop ebp ; Move the base pointer back down
nop

; output
push eax ; save our acc
push ebp ; save our base pointer
mov ebp, esp ; prep the stack frame
push eax; ; give the argument
call 0x7fffffff ; Luckily for us, cdecl stores the result in eax
mov esp, ebp ; Move the stack pointer back down
pop ebp ; Move the base pointer back down
pop eax ; recover our acc
nop

; brz
cmp eax, 0 ; check if eax is zero
jz 0x7fffffff ; XXX: take diff!
nop

; bra
jmp 0x7fffffff  ; XXX: take diff!
nop

; brp
cmp eax, 0 ; check if eax is zero
ja 0x7fffffff ; XXX: take diff!
nop

; add
add eax, [0x7fffffff]
nop

; sub
sub eax, [0x7fffffff]
nop
