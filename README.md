# **inter-cpp**  
> **A blazing-fast, pure C++ bytecode interpreter — engineered for speed, clarity, and total control.**

```ascii
graph TD
    %% Input
    A[Source Code] --> B[Scanner]

    %% Compiler
    B --> C[Parser (Pratt + Recursive Descent)]
    C --> D[Code Generator]
    D --> E[Bytecode Chunk]

    %% Runtime
    E --> F[Virtual Machine (Stack-Based)]
    F --> G[Call Stack (64 frames)]
    F --> H[Heap (GC-managed)]
    H --> I[Objects: String, Function, Closure, Upvalue, Class, Instance, BoundMethod]

    %% GC
    J[Mark Phase] --> K[Trace Roots → Stack → Frames → Upvalues → Globals]
    K --> L[Gray Stack (Tri-color)]
    L --> M[Sweep Phase]
    F --> J

    %% Output
    F --> N[Native Functions (clock)]
    F --> O[Output (stdout)]

    %% Styling
    style A fill:#1e1e2e,stroke:#0f3460,color:#eee
    style O fill:#0f3460,stroke:#1e1e2e,color:#eee
    style F fill:#16213e,stroke:#0f3460,color:#fff
    style J fill:#0a1a2e,stroke:#0f3460,color:#eee
TO RUN -
```ascii
git clone https://github.com/Serosh-commits/inter-cpp.git
cd inter-cpp
mkdir build && cd build
cmake .. && make -j$(nproc)
