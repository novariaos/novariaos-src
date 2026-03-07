.NVM0
; Copy /test.txt to /second.txt
; NOTE: create rootfs/test.txt and rootfs/second.txt files

; Open /test.txt
push 0
push 47 ; '/'
push 116 ; 't'
push 101 ; 'e'
push 115 ; 's'
push 116 ; 't'
push 46  ; '.'
push 116 ; 't'
push 120 ; 'x'
push 116 ; 't'
syscall open
store 0  ; fd1

; Open /second.txt
push 0
push 47 ; '/'
push 115 ; 's'
push 101 ; 'e'
push 99  ; 'c'
push 111 ; 'o'
push 110 ; 'n'
push 100 ; 'd'
push 46  ; '.'
push 116 ; 't'
push 120 ; 'x'
push 116 ; 't'
syscall open
store 1  ; fd2

; read-write loop
loop:
    ; Read test.txt
    load 0
    syscall read
    
    ; Check result
    dup
    push 0
    gt
    
    jz end
    
    ; Write in second.txt
    load 1
    swap
    syscall write
    pop
    
    jmp loop

end:
    pop
    
    push 0
    syscall exit