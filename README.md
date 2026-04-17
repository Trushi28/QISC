<div align="center">

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

[![Version](https://img.shields.io/badge/Version-0.9.0-blueviolet?style=for-the-badge&logo=semver&logoColor=white)](https://github.com/Trushi28/QISC) [![C11](https://img.shields.io/badge/Written_In-C11-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C11_(C_standard_revision)) [![LLVM](https://img.shields.io/badge/Backend-LLVM_21-262D3A?style=for-the-badge&logo=llvm&logoColor=white)](https://llvm.org/) [![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=for-the-badge)](https://opensource.org/licenses/Apache-2.0) [![Status](https://img.shields.io/badge/Status-Advanced_Beta-orange?style=for-the-badge)](https://github.com/Trushi28/QISC)

> _"Optimize for observed reality, not theoretical possibilities."_

</div>

---

Traditional compilers translate source code to machine code in a one-way street. **QISC** rewrites the rules. It introduces the **Living IR** — an Intermediate Representation that _mutates_, _learns from runtime profiles_, and _collapses_ into the single optimal implementation for your actual workload.

Think of it as **Schrödinger's Compiler**: the optimal binary exists in a superposition of possibilities until "observed" through profiling.

> **Current Status — Advanced Beta / Pre-1.0:** The core engine is structurally complete. The Living IR feedback loop, convergence engine, syntax density profiling, auto-parallelization, and personality system are all fully operational. Remaining work (~3–4 months) focuses on heuristic refinement and stdlib expansion toward v1.0.

---

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
| **4. Converge** | Recompile iteratively until IR reaches a fixed point | `qisc build --converge` |

The compiler **provably converges** — given a stable workload, additional compilations produce identical binaries. Your code reaches its _final form_.

---

## ⚡ Quick Start

### Prerequisites

- **GCC** (C11 support)
- **LLVM 21**

### Build from Source

```bash
git clone https://github.com/Trushi28/QISC.git
cd QISC
make
sudo make install  # optional
```

### Or use the prebuilt binary

A prebuilt binary is included in `bin/qisc` if you just want to try it out immediately.

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

---

## 🧬 Language Features

### Expressive Syntax

Functions use `proc`, return values with `give`, and embrace modern constructs:

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

- **Strong static typing** with `int`, `string`, `bool`, `float`
- **Type inference** via `auto` — `auto x = 42;`
- **Constants** via `const` — `const PI = 3;`
- **Optionals** via `maybe` — `maybe string email;`
- **Introspection** — `typeof(x)` and `sizeof(x)` at compile-time
- **Enums** with extension methods via `extend` blocks

### Structs

```qisc
struct Person {
    string name;
    int age;
    maybe string email;
}
```

### Pattern Matching — `when` / `is`

```qisc
when val {
    is 1, 2, 3 { print("Small number"); }
    is > 40    { print("Greater than 40"); }
    else       { print("Other"); }
}
```

### Error Handling — `canfail` / `try` / `catch` / `fail`

Errors are first-class citizens. Functions declare failure with `canfail`, propagate with `!`, and handle with `try`/`catch`:

```qisc
proc read_config(string path) gives Config canfail {
    if !file_exists(path) {
        fail FileNotFound(path);
    }
    auto content = read_file(path)!;
    auto config = parse_json(content)!;
    give Config { host: config["host"], port: config["port"] };
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

---

## 🔗 Pipelines — First-Class Stream Processing

Pipelines in QISC (`>>`) are **not** syntactic sugar. They are first-class stream operations with lazy evaluation, automatic stage fusion, constant memory usage, and natural parallelization:

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

---

## 🧠 The Living IR

The heart of QISC. Unlike traditional static IR, the Living IR is:

| Property | Description |
|----------|-------------|
| **Self-Aware** | Tracks execution frequencies, branch patterns, and cache behavior via `ir_metadata.c` — classifying hot/cold paths with configurable thresholds |
| **Adaptive** | `living_ir.c` (~1,000 lines) analyzes LLVM functions and performs structural mutations — cloning, inlining, and outlining — based on actual profile data |
| **Convergent** | `ir_hash.c` uses FNV-1a structural hashing to detect when mutations have ceased yielding improvements, marking the moment of convergence |
| **Predictive** | Detects dataflow patterns (linear, accumulate, broadcast, scatter) to anticipate optimization opportunities |

### The Evolution Cycle

```
Compilation 1:  Baseline  ──  100% IR change  ──  Collect profile
Compilation 2:  ████████  ──   80% IR change  ──  2x improvement
Compilation 3:  ███████   ──   30% IR change  ──  1.5x additional
Compilation 4:  ██        ──   10% IR change  ──  1.1x additional
Compilation 5:  ▏         ──    0% IR change  ──  CONVERGED ✓
```

### The Single Optimal Path

QISC applies the **90/10 rule** — profile identifies the 10% of code running 90% of the time, then optimizes that 10% to the absolute hardware limit. No bloated binaries with multiple runtime variants.

---

## ⚙️ Optimization Suite

QISC ships with a full suite of profile-driven optimization passes:

**Pipeline Fusion** (`fusion.c`) — Detects and merges pipeline stages (filter→map, map→reduce, filter→map→reduce, and 15+ more patterns) to eliminate intermediate allocations and reduce data passes.

**Tail Call Optimization** (`tail_call.c`) — Detects and transforms self-recursion, mutual recursion, and indirect tail calls into efficient loops. Recursion is iteration in disguise.

**Automatic Memoization** (`memoize.c`) — Profile-driven detection of pure functions with repeated inputs. Automatically caches results without any code changes.

**Function Specialization** (`specialize.c`) — Generates specialized function versions based on observed runtime patterns: array size ranges, constant arguments, value ranges, and null patterns.

**Auto-Parallelization** (`parallel.c`) — Detects parallelizable patterns (map, filter, foreach, map-reduce) and generates POSIX thread code with **work-stealing load balancing**. Independent pipeline stages run in parallel automatically.

---

## 📋 The Pragma System

Pragmas are **suggestions** that guide optimization, not rigid commands. Profile data can override them when reality differs from developer intent.

**Resolution Hierarchy:** `Profile Data > Compiler Knowledge > Pragmas`

### Syntax Directives

```qisc
#pragma style:pipeline     // Pipeline-heavy code following
#pragma style:functional   // Functional composition style
#pragma style:imperative   // Loop-heavy style
```

### Context Directives

```qisc
#pragma context:server     // Long-running → optimize throughput
#pragma context:cli        // Single-run → minimize startup time
#pragma context:embedded   // Resource-limited → optimize size + energy
#pragma context:notebook   // Interactive → incremental compilation
```

### Optimization Directives

```qisc
#pragma optimize:latency     // Minimize response time
#pragma optimize:throughput  // Maximize ops/second
#pragma optimize:memory      // Minimize memory usage
#pragma optimize:size        // Minimize binary size
```

### Pragmas are a Conversation

```
Developer: #pragma optimize:memory
Profile:   Memory is 10MB, system has 16GB. Speed is 50% slower than needed.

Compiler:  "Profile shows speed is critical, not memory.
            Warning: Ignoring optimize:memory
            Suggestion: Consider #pragma optimize:speed instead"
```

---

## 🎭 The Personality Layer

QISC doesn't just compile — it *talks back*. The personality layer is made up of five distinct systems:

### Core Personality Modes

```qisc
#pragma compiler_personality:snarky
```

| Mode | Style | Example |
|------|-------|---------|
| `off` | Silent, professional | _(no commentary)_ |
| `minimal` | Just facts | `Optimizations applied: 47` |
| `friendly` | Encouraging coach | `🎉 Vectorized! You're getting good at this!` |
| `snarky` | Friendly roasts | `O(n²)? In 2026? Really?` |
| `sage` | Wise mentor | `"Recursion without memoization? Bold. Your stack would not approve."` |
| `cryptic` | Easter eggs | `; Here be dragons (actually just cache blocking)` |

### Personality-Aware Error Messages

Error messages adapt to your chosen personality mode. Mistakes feel less like walls and more like guidance:

```
[snarky] TypeError on line 42:
  ┌─────────────────────────────────────────┐
  │  You handed me a string. I wanted int.  │
  │  Bold choice. Wrong, but bold.          │
  └─────────────────────────────────────────┘
```

### Tiny LLM — Markov Chain Commentary Engine

QISC ships with a custom **Markov chain text generator** written in C (`tiny_llm.c`). No external dependencies, no multi-GB model downloads — just a lightweight n-gram engine pre-seeded with compiler humor, existential musings, and developer roasts. It learns from your compilation history and gets more contextually accurate over time, saving its state to `~/.qisc/llm_state.json`.

```
"Your code doesn't just have bugs, it's a whole ecosystem."
"I parse, therefore I am."
"I have no context window, only vibes."
```

### Easter Eggs in Assembly

`easter_eggs.c` injects fun messages directly into compiled assembly output — inspired by engineers etching messages into silicon chips. Curious debugger users get rewarded.

### Panic Mode

When compilation goes to hell:

```
⚠️  WE'RE ON THE LAST LEG HERE ⚠️

Memory: 99.4% used
Swap: Thrashing like a death metal concert
CPU: Thermal throttling engaged

Your computer is begging for mercy.
Continuing anyway because I'm either brave or stupid.
```

---

## 🏆 Achievement & Progression System

QISC tracks your development journey persistently across all sessions in `~/.qisc/`. Two systems work together:

### Achievements (50+)

Organized across five categories — Compilation, Optimization, Exploration, Mastery, and Fun:

```
🏆 Achievement Unlocked!
╔══════════════════════════════════════════╗
║  🐉 Dragon Slayer                        ║
║  Fixed O(n²) to O(n log n)              ║
╚══════════════════════════════════════════╝
```

Some fan favorites:

| Achievement | Condition |
|-------------|-----------|
| 🌙 **Night Owl** | Compiled between midnight and 4 AM |
| ⚠️ **Living Dangerously** | Compiled on a Friday afternoon |
| 🔥 **RAM Dancer** | Completed compilation at 99%+ memory usage |
| 🎂 **Pi Enthusiast** | Compiled on March 14th |
| 👻 **Spooky Coder** | Compiled on Halloween |
| 💀 **So Close!** | Compilation crashed at 99%+ |
| 🏆 **Completionist** | Unlocked all achievements |

### Competitive / Leaderboard

Track your stats against other QISC users with persistent titles earned through compilation milestones:

```
Transcendent Developer  →  1000+ compilations
Code Sage               →  500+  compilations
Compiler Whisperer      →  100+  compilations
Optimization Enthusiast →  50+   compilations
```

Stats, streaks, and comparisons are saved to `~/.qisc/stats.json` and `~/.qisc/leaderboard.json`.

---

## 🗒️ Notebook Mode

QISC supports interactive notebook execution (`.qnb` files) with incremental cell compilation — think Jupyter but for systems programming:

```bash
qisc notebook demo.qnb
```

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────┐
│                    QISC Compiler                    │
├─────────────┬──────────────┬────────────────────────┤
│  Frontend   │  Middle-End  │        Backend         │
│             │              │                        │
│  Lexer      │  Living IR   │  LLVM 21 IR Gen        │
│  Parser     │  IR Metadata │  Optimization Passes   │
│  Typechecker│  Profile     │  Native Binary         │
│             │  Specialize  │                        │
├─────────────┴──────────────┴────────────────────────┤
│              Optimization Suite                     │
│  Fusion  │  TCO  │  Memoize  │  Parallel            │
├──────────────────────────────────────────────────────┤
│              Personality Layer                      │
│  Personality  │  Error Messages  │  Tiny LLM        │
│  Easter Eggs  │  Achievements    │  Competitive     │
├──────────────────────────────────────────────────────┤
│              Runtime                                │
│  Streams  │  Arrays  │  Error/setjmp  │  I/O        │
└──────────────────────────────────────────────────────┘
```

### Source Layout

```
QISC/
├── src/
│   ├── main.c
│   ├── lexer/              # Tokenizer
│   ├── parser/             # Recursive descent parser + AST
│   ├── typechecker/        # Static type checking with inference
│   ├── ir/                 # Living IR, IR hash, IR metadata
│   ├── codegen/            # LLVM IR code generation (6,300+ lines)
│   ├── optimization/       # Fusion, TCO, memoize, specialize, parallel
│   ├── profile/            # Runtime profiling and convergence
│   ├── syntax/             # Syntax density profiling (~50KB)
│   ├── personality/        # Personality, error messages, tiny LLM, easter eggs
│   ├── achievements/       # Achievement system (50+ achievements)
│   ├── pragma/             # Pragma validation and processing
│   ├── interpreter/        # Direct interpretation mode
│   ├── cli/                # CLI interface + REPL + notebook mode
│   ├── runtime/            # Streams, arrays, error handling, I/O
│   └── utils/              # Shared utilities
├── include/
│   └── qisc.h
├── examples/               # Example .qisc programs
├── stdlib/                 # Standard library
├── bin/qisc                # Prebuilt binary
└── Makefile
```

---

## 🔧 Usage

```bash
# Run directly
qisc run program.qisc

# Build binary
qisc build program.qisc

# Build with profiling instrumentation
qisc build --profile program.qisc

# Auto-converge to optimal binary
qisc build --converge

# Interactive REPL
qisc repl

# Notebook mode
qisc notebook demo.qnb

# Release build
make DEBUG=0
```

---

## 📂 Examples

The `examples/` directory contains working programs demonstrating QISC features:

| File | Demonstrates |
|------|-------------|
| `hello.qisc` | Basic structure, `proc`, `give`, pragmas |
| `fibonacci.qisc` | Functions, loops, conditionals |
| `pipeline.qisc` | `>>` pipelines, `filter`/`map`/`reduce` |
| `pattern_match.qisc` | `when`/`is` pattern matching |
| `error_handling.qisc` | `canfail`, `try`/`catch`, `fail`, `!` propagation |
| `advanced_features.qisc` | `const`, `auto`, `typeof`, `sizeof`, enums, `extend` |
| `lazy_stream_*.qisc` | Lazy evaluation, stream transforms, cross-map, lambdas |
| `pragma_test.qisc` | Pragma directives and context switching |
| `notebook_demo.qnb` | Notebook mode with incremental cell execution |

---

## 🗺️ Roadmap

### ✅ Implemented

- [x] Lexer, parser, typechecker — full frontend pipeline
- [x] LLVM 21 backend code generation
- [x] Full language: `proc`/`give`, pattern matching, `canfail`/`try`/`catch`, pipelines, lambdas, structs, enums, modules
- [x] Living IR — profile-driven mutation engine (`living_ir.c`)
- [x] Convergence loop with structural hashing (`ir_hash.c`, `ir_metadata.c`)
- [x] `qisc build --converge` — functional fixed-point compilation
- [x] Syntax density profiling — style-aware optimization (`syntax_profile.c`, ~50KB)
- [x] Pipeline fusion (15+ fusion patterns)
- [x] Tail Call Optimization
- [x] Automatic memoization (profile-driven)
- [x] Function specialization from runtime patterns
- [x] Auto-parallelization with work-stealing (`parallel.c`, POSIX threads)
- [x] Full pragma system (style, context, optimize, behavior)
- [x] Personality system with 5 modes
- [x] Personality-aware error messages
- [x] Tiny LLM — Markov chain commentary engine with persistence
- [x] Easter egg system (injected into assembly output)
- [x] Achievement system (50+ achievements, persistent)
- [x] Competitive leaderboard with developer titles
- [x] REPL and notebook mode (`.qnb`)
- [x] Runtime stdlib — streams, arrays, error handling (setjmp/longjmp), I/O

### 🔜 In Progress / Planned

- [ ] Heuristic refinement — smarter Living IR mutation decisions
- [ ] Auto-parallelization stabilization
- [ ] Auto-vectorization (`vectorize:auto`)
- [ ] Standard library (`stdlib/`) expansion
- [ ] Platform-specific optimization expansion

---

## 🤝 Contributing

QISC is experimental and under active development. Contributions, ideas, and feedback are welcome.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-idea`)
3. Commit your changes (`git commit -m 'Add your idea'`)
4. Push to the branch (`git push origin feature/your-idea`)
5. Open a Pull Request

---

## 📄 License

Licensed under the **Apache License 2.0** — see the [LICENSE](LICENSE) file for details.

---

<div align="center">

_Built with C11 and LLVM 21. 36,000+ lines. Zero compromises._

**QISC** — _It's not just a translator. It's an optimization partner that gets better over time._

```
"Your code has reached its final form."
                    — QISC, upon convergence
```

</div>
