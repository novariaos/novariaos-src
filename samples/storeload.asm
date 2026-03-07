.NVM0
; Test memory operations (store/load)

; Store different values in local variables
push 100
store 0         ; locals[0] = 100

push 200
store 1         ; locals[1] = 200

push 300
store 2         ; locals[2] = 300

; Load and verify
load 0          ; Should be 100
break

load 1          ; Should be 200  
break

load 2          ; Should be 300
break

; Test arithmetic with stored values
load 0
load 1
add             ; 100 + 200 = 300
break

load 1
load 2
sub             ; 200 - 300 = -100
break

syscall exit
hlt