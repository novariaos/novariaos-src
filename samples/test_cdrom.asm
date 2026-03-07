.NVM0
; Test program to open and read from /dev/cdrom0

; --- Push "/dev/cdrom0" onto the stack ---
push 0      ; Null terminator
push '/'
push 'd'
push 'e'
push 'v'
push '/'
push 'c'
push 'd'
push 'r'
push 'o'
push 'm'
push '0'

; --- Open the file ---
syscall open
store 0     ; fd is now in local 0, stack is empty

; --- Check if open was successful ---
load 0      ; PUSH fd from local 0 onto the stack
dup         ; Duplicate the fd to check it
push 0
lt          ; Check if fd < 0 (error)
jnz fail    ; If so, jump to the fail label

; If we are here, open was successful. The stack still has the fd on it.
pop         ; Clean up the fd from the stack.

; --- Print a success message ---
push 'O'
syscall print
push 'K'
syscall print
push ' '
syscall print


; --- Read loop (read and print 16 bytes) ---
push 16
store 1     ; counter = 16

read_loop:
    ; check counter
    load 1
    push 0
    eq          ; check if counter == 0
    jnz end_read_loop ; if counter == 0, exit loop

    ; Read one byte from cdrom0
    load 0      ; Push fd
    syscall read
    
    ; The result of read is on the stack.
    ; syscall 'print' takes its argument from the stack.
    dup ; duplicate the value to check for EOF
    push 0
    eq
    jnz end_read_loop ; if byte is 0 (EOF), exit.

    syscall print

    ; decrement counter
    load 1
    push 1
    sub
    store 1
    jmp read_loop

end_read_loop:

; --- Newline and exit ---
push '\n'
syscall print
push 0
syscall exit

; --- Failure path ---
fail:
    ; Print "FAIL "
    push 'F'
    syscall print
    push 'A'
    syscall print
    push 'I'
    syscall print
    push 'L'
    syscall print
    push ' '
    syscall print
    ; The error code is on the stack, just exit with it.
    syscall exit
