+++
title = 'Showcase'
comments = false
+++

### **cielc** ([GitHub](https://github.com/v1bh475u/ciel))
Custom RISC-V compiler. Built front-end (Flex/Bison) and generated optimized intermediate code supporting classes, inheritance, and lambdas. Implemented linear-scan register allocation for speed. *(Tech: C++, RISC-V ISA, Flex/Bison).*

---

### **fenris** ([GitHub](https://github.com/v1bh475u/fenris))
Networked file system with thread-safe caching and encrypted communication. Implemented ECDH key exchange and AES-GCM for security, added zlib compression and LRU caching for throughput, and used Protobuf for messaging. *(Tech: C++, Protobuf, CryptoPP, zlib).*

---


### **spinlock benchmark** ([GitHub](https://github.com/v1bh475u/spinlock_benchmark))
Implemented multiple spinlock algorithms (CAS loop and ticket lock) in C++ and built a Google Benchmark suite to compare their performance under contention. This project measures throughput/latency against std::mutex to analyze synchronization efficiency. *(Tech: C++, `<atomic>`, Google Benchmark, CMake).*

---

### **mydbg** ([GitHub](https://github.com/v1bh475u/mydbg))
Custom debugger using `ptrace`. Implemented breakpoints, symbol resolution, register/memory inspection akin to gdb. Emphasized modular design for maintainability. *(Tech: C++, Linux `ptrace` API).*

---

### **gbemu** ([GitHub](https://github.com/sdslabs/gbemu))
Game Boy emulator (audio subsystem). Refactored audio unit to separate logic, implemented cycle-accurate timing and channel mixing per Pan Docs, improving code clarity. *(Tech: C++, SDL2).*

---


