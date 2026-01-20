.NVM0

; ARGV
; ARGC
pop

push 0
push '/'
push 'd'
push 'e'
push 'v'
push '/'
push 't'
push 't'
push 'y'
syscall open
store 1  ; fd2

loop:
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
    push 10
    load 1
    swap
    syscall write
    pop
    pop
    
    push 0
    syscall exit