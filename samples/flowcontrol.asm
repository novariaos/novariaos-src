.NVM0
; Test flow control (jumps, loops, calls)

; Siloop counting from 5 to 1
push 5
store 0          ; Store counter in local variable 0

loop_start:
    load 0       ; Load counter
    push 0
    gt           ; Check if counter > 0
    jnz loop_body
    jmp loop_end

loop_body:
    load 0       ; Print current counter value
    break
    
    ; Decrement counter
    load 0
    push 1
    sub
    store 0
    
    jmp loop_start

loop_end:
    push 999        ; Loop finished marker

syscall exit