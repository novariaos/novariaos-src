.NVM0

push 0
push '/'
push 'd'
push 'e'
push 'v'
push '/'
push 'u'
push 'r'
push 'a'
push 'n'
push 'd'
push 'o'
push 'm'
syscall open
store 0

load 0
syscall read
syscall exit