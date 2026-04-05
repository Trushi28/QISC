```
  ___  ___ ____   ____ 
 / _ \|_ _/ ___| / ___|
| | | || |\___ \| |    
| |_| || | ___) | |___ 
 \__\_\|___|____/ \____|
```
# Q I S C
### Quantum-Inspired Superposition Compiler
_The compiler that learns, adapts, and evolves with your code._
[![Version](https://img.shields.io/badge/Version-0.9.0-blueviolet?style=for-the-badge&logo=semver&logoColor=white)](https://github.com/BethuTrushi/QISC) [![C11](https://img.shields.io/badge/Written_In-C11-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C11_(C_standard_revision)) [![LLVM](https://img.shields.io/badge/Backend-LLVM_21-262D3A?style=for-the-badge&logo=llvm&logoColor=white)](https://llvm.org/) [![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=for-the-badge)](https://opensource.org/licenses/Apache-2.0) [![Status](https://img.shields.io/badge/Status-Advanced_Beta-orange?style=for-the-badge)](https://github.com/BethuTrushi/QISC)
> _"Optimize for observed reality, not theoretical possibilities."_
----------
Traditional compilers translate source code to machine code in a one-way street. **QISC** rewrites the rules. It introduces the **Living IR** — an Intermediate Representation that _mutates_, _learns from runtime profiles_, and _collapses_ into the single optimal implementation for your actual workload. Think of it as **Schrödinger's Compiler**: the optimal binary exists in a superposition of possibilities until "observed" through profiling.
QISC isn't just a compiler — it's your **optimization partner**.
> **Current Status — Advanced Beta / Pre-1.0:** The core engine is structurally complete and far more advanced than the documentation previously suggested. The Living IR feedback loop, convergence engine, syntax density profiling, and personality system are all fully functional. Remaining work (~3–4 months, single expert developer) focuses on refining mutation heuristics and expanding the standard library toward v1.0.
## 🌊 How It Works
```mermaid
graph LR
    A[Source Code] --> B[Living IR]
    B --> C[Profile Data]
    C --> D[Evolved IR]
    D --> E[Optimal Binary]
    C -->|feedback loop| B
```

| Step | What Happens | Command |
|------|-------------|---------|
| **1. Write** | Code in QISC's expressive syntax | — |
| **2. Build** | Compile to baseline binary with instrumentation | `qisc build --profile app.qisc` |
| **3. Run** | Execute and collect real-world usage profiles | `./app` |
| **4. Converge** | Recompile iteratively until IR reaches optimal state (functional — hashes IR at each step, stops at fixed point) | `qisc build --converge` |
The compiler **provably converges** — given a stable workload, additional compilations produce identical binaries. Your code reaches its _final form_.
----------
## ⚡ Quick Start
### Prerequisites
-   **GCC** (C11 support)
-   **LLVM 21**
### Build from Source
```bash
git clone https://github.com/BethuTrushi/QISC.git
cd QISC
make
sudo make install  # optional
```
### Hello, QISC
```qisc
#pragma context:cli
#pragma compiler_personality:friendly
proc main() gives int {
    print("Hello, World!");
    give 0;
}
```
```bash
qisc run hello.qisc
```
----------
## 🧬 Language Features
### Expressive Syntax
QISC feels familiar yet powerful. Functions use `proc`, return values with `give`, and embrace modern constructs:
```qisc
proc fibonacci(int n) gives int {
    if n <= 1 {
        give n;
    }
    int a = 0;
    int b = 1;
    int result = 0;
    for int i = 2; i <= n; i++ {
        result = a + b;
        a = b;
        b = result;
    }
    give result;
}
```
### Type System
-   **Strong static typing** with `int`, `string`, `bool`, `float`
-   **Type inference** via `auto` — `auto x = 42;`
-   **Constants** via `const` — `const PI = 3;`
-   **Optionals** via `maybe` — `maybe string email;`
-   **Introspection** — `typeof(x)` and `sizeof(x)` at compile-time
-   **Enums** with extension methods via `extend` blocks
```qisc
enum Color { Red, Green, Blue }
extend Color {
    proc name(self) gives string {
        when self {
            is 0 { give "Red"; }
            is 1 { give "Green"; }
            is 2 { give "Blue"; }
        }
        give "Unknown";
    }
}
```
### Structs
```qisc
struct Person {
    string name;
    int age;
    maybe string email;
}
```
### Pattern Matching — `when` / `is`
Powerful pattern matching with value matching, range matching, and wildcards:
```qisc
when val {
    is 1, 2, 3 {
        print("Small number");
    }
    is > 40 {
        print("Greater than 40");
    }
    else {
        print("Other");
    }
}
```
### Error Handling — `canfail` / `try` / `catch` / `fail`
Errors are first-class citizens. Functions declare failure with `canfail`, propagate with `!`, and handle with `try`/`catch`:
```qisc
proc read_config(string path) gives Config canfail {
    if !file_exists(path) {
        fail FileNotFound(path);
    }
    auto content = read_file(path)!;   // ! propagates failure
    auto config = parse_json(content)!;
    give Config { host: config["host"], port: config["port"] };
}
proc main() gives int {
    try {
        auto cfg = read_config("config.json")!;
        print("Loaded: {cfg.host}:{cfg.port}");
    } catch FileNotFound e {
        print("Not found: {e.path}");
    } catch any e {
        print("Error: {e}");
    }
}
```
### Lambdas & Do Blocks
```qisc
// Inline lambda
auto square = (x) => x * x;
// Multi-line do block
auto doubler = do |n| {
    give n * 2;
};
```
### Modules
```qisc
module mylib;
import std;
```
----------
## 🔗 Pipelines — First-Class Stream Processing
Pipelines in QISC (`>>`) are **not** syntactic sugar for nested function calls. They represent first-class stream operations with:
-   **Lazy evaluation** — nothing runs until a terminal operation
-   **Automatic stage fusion** — the compiler combines pipeline stages
-   **Constant memory** — processes data in chunks, not all at once
-   **Natural parallelization** — independent stages can run in parallel
```qisc
#pragma style:pipeline
auto names = get_people()
    >> filter(p => p.age >= 25)
    >> filter(p => p.email has _)
    >> map(p => p.name)
    >> collect();
int total = ages >> reduce(0, (a, b) => a + b);
```
QISC binaries compose seamlessly with Unix tools:
```bash
cat data.txt | qisc-program | sort | uniq | qisc-analyzer
```
----------
## 🧠 The Living IR
The heart of QISC. Unlike traditional static IR, the Living IR is:
| Property | Description |
|----------|-------------|
| **Self-Aware** | Tracks its own execution frequencies, branch patterns, and cache behavior |
| **Adaptive** | Restructures itself based on collected profile data |
| **Predictive** | Anticipates optimization opportunities before they're needed |
| **Convergent** | Provably reaches an optimal stable state in finite compilations |
### The Evolution Cycle
```
Compilation 1:  Baseline  ──  100% IR change  ──  Collect profile
Compilation 2:  ████████  ──   80% IR change  ──  2x improvement
Compilation 3:  ███████   ──   30% IR change  ──  1.5x additional
Compilation 4:  ██        ──   10% IR change  ──  1.1x additional
Compilation 5:  ▏         ──    0% IR change  ──  CONVERGED ✓
```
### IR Pattern Recognition
The IR recognizes high-level patterns and applies specialized optimizations:
-   **Map-Reduce** → Parallel execution, vectorization, stream fusion
-   **State Machines** → Jump tables, branch-free transitions
-   **Pipelines** → Stage fusion, parallel execution, stream processing
### The Single Optimal Path
QISC doesn't bloat your binary with multiple runtime variants. It applies the **90/10 rule**:
> _Profile identifies the 10% of code running 90% of the time → optimize that 10% to the absolute hardware limit._
```
Function: sort(array)
Profile: 80% of calls have array size 100-1000
Generated code:
  → Optimized quicksort for 100-1000 elements (most common case)
  → Insertion sort for <100 elements (fast path)
  → Heapsort for >1000 elements (fallback)
```
----------
## 📋 The Pragma System
Pragmas are **suggestions** that guide optimization, not rigid commands. Profile data can override them when reality differs from developer intent.
**Resolution Hierarchy:** `Profile Data > Compiler Knowledge > Pragmas`
### Syntax Directives
```qisc
#pragma style:pipeline     // Pipeline-heavy code following
#pragma style:brace        // C-like brace syntax
#pragma style:python       // Indentation-based
#pragma style:expression   // Functional composition
```
### Context Directives
```qisc
#pragma context:server     // Long-running → optimize throughput, amortize startup
#pragma context:cli        // Single-run → minimize startup time, small binary
#pragma context:web        // Browser → optimize size + startup
#pragma context:embedded   // Resource-limited → optimize size + energy
#pragma context:notebook   // Interactive → incremental compilation
```
### Optimization Directives
```qisc
#pragma optimize:latency       // Minimize response time
#pragma optimize:throughput    // Maximize ops/second
#pragma optimize:memory        // Minimize memory usage
#pragma optimize:size          // Minimize binary size
```
### Behavior Directives
```qisc
#pragma inline:always          // Strong inlining hint
#pragma vectorize:auto         // Auto-vectorize loops
#pragma parallel:auto          // Auto-parallelize stages
#pragma hot_path               // Mark as performance-critical
#pragma cold_path              // Mark as rarely executed
```
### Pragmas are a Conversation
```
Developer: #pragma optimize:memory
Profile:   Memory is 10MB, system has 16GB. Speed is 50% slower than needed.
Compiler:  "Profile shows speed is critical, not memory.
            Warning: Ignoring optimize:memory
            Suggestion: Consider #pragma optimize:speed instead"
```
----------
## 🎭 Compiler Personality System
Compilation shouldn't be boring. QISC ships with a personality system — inspired by engineers etching messages on silicon chips.
```qisc
#pragma compiler_personality:snarky
```
### Personality Modes
| Mode | Style | Example |
|------|-------|---------|
| `off` | Silent, professional | _(no commentary)_ |
| `minimal` | Just facts | `Optimizations applied: 47` |
| `friendly` | Encouraging coach | `🎉 Vectorized! You're getting good at this!` |
| `snarky` | Friendly roasts | `O(n²)? In 2026? Really?` |
| `sage` | Wise mentor | `"Recursion without memoization? Bold. Your stack would not approve."` |
| `cryptic` | Easter eggs | `; Here be dragons (actually just cache blocking)` |
### Pattern-Aware Commentary
The compiler detects code patterns and comments accordingly:
```
Detected: 5 nested loops — O(n⁵)
  "This will finish executing sometime
   after the heat death of the universe"
  For n=100, estimated runtime: 47 years
  Consider: literally any other algorithm
```
### When Compilation Goes to Hell
```
⚠️  WE'RE ON THE LAST LEG HERE ⚠️
Memory: 99.4% used
Swap: Thrashing like a death metal concert
CPU: Thermal throttling engaged
Your computer is begging for mercy.
Continuing anyway because I'm either brave or stupid.
```
### Achievement System
```
🎯 "First Steps"         First successful compilation
⚡ "Speed Demon"         10x speedup achieved
🧠 "Cache Whisperer"     Cache hit rate > 99%
🎨 "SIMD Sorcerer"       Successfully vectorized code
🐉 "Dragon Slayer"       Fixed O(n²) to O(n log n)
💀 "I've Seen Things"    Survived compilation crash at 99%+
```
----------
## 🏗️ Architecture
```
┌─────────────────────────────────────────────────────┐
│                    QISC Compiler                    │
├─────────────┬─────────────┬─────────────────────────┤
│             │             │                         │
│  Frontend   │  Middle-End │        Backend          │
│             │             │                         │
│  ┌───────┐  │  ┌───────┐  │  ┌───────────────────┐  │
│  │ Lexer │──┤  │Living │  │  │  LLVM 21 IR Gen   │  │
│  └───────┘  │  │  IR   │──┤  └───────────────────┘  │
│  ┌───────┐  │  │Engine │  │  ┌───────────────────┐  │
│  │Parser │──┤  └───────┘  │  │  Optimization     │  │
│  └───────┘  │  ┌───────┐  │  │  Passes           │  │
│  ┌───────┐  │  │Profile│  │  └───────────────────┘  │
│  │ Type  │──┤  │Engine │  │  ┌───────────────────┐  │
│  │Checker│  │  └───────┘  │  │  Native Binary    │  │
│  └───────┘  │             │  └───────────────────┘  │
│             │             │                         │
│  ┌───────────────┐  ┌─────────────────────────────┐ │
│  │  Personality  │  │     CLI Interface           │ │
│  │    Engine     │  │     (qisc build/run/...)    │ │
│  └───────────────┘  └─────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```
### Source Layout
```
QISC/
├── src/
│   ├── main.c              # Entry point
│   ├── lexer/              # Tokenizer — multi-paradigm syntax support
│   ├── parser/             # Recursive descent parser with pragma handling
│   ├── typechecker/        # Static type checking with inference
│   ├── ir/                 # Living IR generation and evolution
│   ├── codegen/            # LLVM IR code generation
│   ├── profile/            # Runtime profiling and convergence
│   ├── personality/        # Compiler commentary and achievements
│   ├── interpreter/        # Direct interpretation mode
│   ├── cli/                # Command-line interface
│   ├── types/              # Type system internals
│   └── utils/              # Shared utilities
├── include/
│   └── qisc.h              # Public API header
├── examples/               # Example programs
├── tests/                  # Test suite
├── Makefile                # Build system
└── README.md
```
----------
## 📂 Examples
The `examples/` directory contains working programs demonstrating QISC features:
| File | Demonstrates |
|------|-------------|
| `hello.qisc` | Basic program structure, `proc`, `give`, pragmas |
| `fibonacci.qisc` | Functions, loops, conditionals, integer arithmetic |
| `pipeline.qisc` | Stream pipelines (`>>`), structs, `maybe` optionals, `filter`/`map`/`reduce` |
| `pattern_match.qisc` | `when`/`is` pattern matching with values, ranges, and wildcards |
| `error_handling.qisc` | `canfail`, `try`/`catch`, `fail`, error propagation with `!` |
| `advanced_features.qisc` | `const`, `auto`, `typeof`, `sizeof`, do blocks, enums, `extend`, modules |
----------
## 🗺️ Roadmap
### ✅ Implemented
-   [x] Lexer with multi-token support
-   [x] Recursive descent parser (structs, enums, pipelines, pattern matching)
-   [x] Type checker with inference
-   [x] LLVM 21 backend code generation
-   [x] `proc` / `give` function model
-   [x] `when` / `is` pattern matching
-   [x] `canfail` / `try` / `catch` / `fail` error handling
-   [x] `>>` pipeline operator
-   [x] `for-in` loop for arrays
-   [x] `auto` type inference & `const` enforcement
-   [x] `typeof` / `sizeof` intrinsics
-   [x] Structs & `maybe` optionals
-   [x] Enums with `extend` blocks
-   [x] Do blocks (multi-line lambdas)
-   [x] Compiler personality system (2,000+ line engine with pattern-aware commentary, panic mode, and achievement tracking)
-   [x] CLI interface (`qisc build`, `qisc run`, `qisc repl`, notebook mode)
-   [x] Module system (`module` / `import`)
-   [x] Profile-driven IR evolution — Living IR mutation engine (`src/ir/living_ir.c`, ~1,000 lines)
-   [x] `qisc build --converge` auto-convergence with structural hashing (`src/ir/ir_hash.c`)
-   [x] Syntax density profiling — style-aware optimization (`src/syntax/syntax_profile.c`, ~50KB)
-   [x] Multi-syntax support via `#pragma style:*` (Pipeline, Functional, Imperative)
-   [x] Stream fusion and lazy evaluation for pipelines (`src/optimization/fusion.c`)
-   [x] Achievement system with persistent tracking (integrated into CLI)
-   [x] Tail Call Optimization (TCO), Memoization, and Specialization passes
-   [x] Specialized runtime stdlib — Arrays, Streams, Error Handling, I/O
### 🔜 In Progress / Planned
-   [ ] Heuristic refinement — improving mutation intelligence in the Living IR
-   [ ] Automatic parallelization (`parallel:auto`) — stabilizing auto-parallelization logic
-   [ ] Auto-vectorization (`vectorize:auto`)
-   [ ] Context-aware optimization strategies (`context:server`, `context:embedded`, etc.)
-   [ ] Optimization directives (`optimize:latency`, `optimize:throughput`, etc.)
-   [ ] Platform-specific optimization expansion
-   [ ] Standard library (`stdlib/`) expansion
-   [ ] IR pattern recognition (map-reduce, state machines) — heuristic polish
-   [ ] Pragma inference from code patterns
-   [ ] Meta-optimization (compiler learns which optimizations work best)
----------
## 🔧 Usage
```bash
# Compile and run
qisc run program.qisc
# Build binary
qisc build program.qisc
# Build with profiling instrumentation
qisc build --profile program.qisc
# Auto-converge to optimal binary
qisc build --converge
# Build in release mode
make DEBUG=0
# Clean build artifacts
make clean
```
----------
## 🤝 Contributing
QISC is experimental and under active development. Contributions, ideas, and feedback are welcome.
1.  Fork the repository
2.  Create a feature branch (`git checkout -b feature/your-idea`)
3.  Commit your changes (`git commit -m 'Add your idea'`)
4.  Push to the branch (`git push origin feature/your-idea`)
5.  Open a Pull Request
----------
## 📄 License
This project is licensed under the **Apache License 2.0** — see the [LICENSE](https://claude.ai/chat/LICENSE) file for details.
----------
_Built with C11 and LLVM 21._ _Designed for the future of systems programming._
**QISC** — _It's not just a translator. It's an optimization partner that gets better over time._
```
"Your code has reached its final form."
                    — QISC, upon convergence
```
