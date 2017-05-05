# StackShrink test
StackShrink test

Original code from https://www.codeproject.com/Articles/12644/How-to-release-memory-pages-occupied-by-the-thread

Output:

```
### Stack Dump Start
### Range: 0x100000000 - 0x1000fa000 Protect = 00000000 State = RESERVE Pages = 250
### Range: 0x1000fa000 - 0x1000fd000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x1000fd000 - 0x100100000 Protect = 0x000004 State = COMMIT  Pages = 3
### Stack Dump Finish
Default stack

### Stack Dump Start
### Range: 0x100000000 - 0x1000e3000 Protect = 00000000 State = RESERVE Pages = 227
### Range: 0x1000e3000 - 0x1000e6000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x1000e6000 - 0x100100000 Protect = 0x000004 State = COMMIT  Pages = 26
### Stack Dump Finish
100K consumed

### Stack Dump Start
### Range: 0x100000000 - 0x1000fd000 Protect = 00000000 State = RESERVE Pages = 253
### Range: 0x1000fd000 - 0x1000fe000 Protect = 0x000104 State = COMMIT  Pages = 1
### Range: 0x1000fe000 - 0x100100000 Protect = 0x000004 State = COMMIT  Pages = 2
### Stack Dump Finish
Stack compacted

### Stack Dump Start
### Range: 0x100000000 - 0x10001b000 Protect = 00000000 State = RESERVE Pages = 27
### Range: 0x10001b000 - 0x10001e000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x10001e000 - 0x100100000 Protect = 0x000004 State = COMMIT  Pages = 226
### Stack Dump Finish
900K consumed

### Stack Dump Start
### Range: 0x100000000 - 0x1000fd000 Protect = 00000000 State = RESERVE Pages = 253
### Range: 0x1000fd000 - 0x1000fe000 Protect = 0x000104 State = COMMIT  Pages = 1
### Range: 0x1000fe000 - 0x100100000 Protect = 0x000004 State = COMMIT  Pages = 2
### Stack Dump Finish
Stack compacted
```