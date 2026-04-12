; @AIused
; NVM heap test - write "hey" to /dev/tty using heap

.NVM0

start:
    ; Store string "hey" in heap at offset 0
    ; First character 'h'
    PUSH 0          ; offset
    PUSH 'h'        ; value
    STORE_HEAP      ; store 'h' at offset 0
    
    ; Second character 'e'
    PUSH 1          ; offset
    PUSH 'e'        ; value
    STORE_HEAP      ; store 'e' at offset 1
    
    ; Third character 'y'
    PUSH 2          ; offset
    PUSH 'y'        ; value
    STORE_HEAP      ; store 'y' at offset 2

    ; Third character 'y'
    PUSH 3          ; offset
    PUSH 10       ; value
    STORE_HEAP      ; store 'y' at offset 2
    
    ; Null terminator at offset 3
    PUSH 4          ; offset
    PUSH 0          ; null terminator
    STORE_HEAP      ; store 0 at offset 3
    
    ; Write to stdout (fd=1)
    ; SYS_WRITE expects: [fd, offset] (offset on top)
    PUSH 1          ; fd = stdout
    PUSH 0          ; offset = 0 (start of string in heap)
    SYSCALL write   ; write "hey" to stdout
    
    ; Exit with code 0
    PUSH 0
    SYSCALL exit

end:
    HLT