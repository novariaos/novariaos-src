.NVM

push 0
push '/'
push 'p'
push 'r'
push 'o'
push 'c'
push '/'
push 'u'
push 'p'
push 't'
push 'i'
push 'm'
push 'e'
syscall open
store 0

loop:
    push 256
    load 0
    syscall read

    dup
    push 0
    eq
    
    jnz end
    
    syscall print
    pop      
    
    jmp loop

end:
    pop
    load 0
    
    push 0
    syscall exit