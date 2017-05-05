# StackShrink test
StackShrink test

Original code from https://www.codeproject.com/Articles/12644/How-to-release-memory-pages-occupied-by-the-thread

Output:

### Stack Dump Start
### Range: 0x38fce70000 - 0x38fcf6a000 Protect = 00000000 State = RESERVE Pages = 250
### Range: 0x38fcf6a000 - 0x38fcf6d000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x38fcf6d000 - 0x38fcf70000 Protect = 0x000004 State = COMMIT  Pages = 3
### Stack Dump Finish
Default stack

### Stack Dump Start
### Range: 0x38fce70000 - 0x38fcf53000 Protect = 00000000 State = RESERVE Pages = 227
### Range: 0x38fcf53000 - 0x38fcf56000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x38fcf56000 - 0x38fcf70000 Protect = 0x000004 State = COMMIT  Pages = 26
### Stack Dump Finish
100K consumed

### Stack Dump Start
### Range: 0x38fce70000 - 0x38fcf6a000 Protect = 00000000 State = RESERVE Pages = 250
### Range: 0x38fcf6a000 - 0x38fcf6d000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x38fcf6d000 - 0x38fcf70000 Protect = 0x000004 State = COMMIT  Pages = 3
### Stack Dump Finish
Stack compacted

### Stack Dump Start
### Range: 0x38fce70000 - 0x38fce8b000 Protect = 00000000 State = RESERVE Pages = 27
### Range: 0x38fce8b000 - 0x38fce8e000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x38fce8e000 - 0x38fcf70000 Protect = 0x000004 State = COMMIT  Pages = 226
### Stack Dump Finish
900K consumed

### Stack Dump Start
### Range: 0x38fce70000 - 0x38fcf6a000 Protect = 00000000 State = RESERVE Pages = 250
### Range: 0x38fcf6a000 - 0x38fcf6d000 Protect = 0x000104 State = COMMIT  Pages = 3
### Range: 0x38fcf6d000 - 0x38fcf70000 Protect = 0x000004 State = COMMIT  Pages = 3
### Stack Dump Finish
Stack compacted

