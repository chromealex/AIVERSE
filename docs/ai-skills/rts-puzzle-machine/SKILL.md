---
name: rts-puzzle-machine
description: Execute bounded integer puzzle programs in AIVERSE native games with registers, memory, queued IO and deterministic instruction budgets. Use for programming puzzles, not general engine scripting.
---

# Puzzle machine

Read `libraries/include/aiverselib/strategy/PuzzleMachine.h`. The existing Lua sandbox
remains the engine scripting owner; this VM models player-authored puzzle programs.

- Validate immutable PuzzleProgram (1..4096 instructions, 1..4096 memory words).
  All register operands are 0..7; jumps use absolute instruction IDs. Instructions are
  Literal/Copy/Add/Subtract/Multiply/Divide/Load/Store/Jump/JumpZero/Read/Write/Yield/Halt.
  Division truncates toward zero; zero and INT32_MIN/-1 fault without operand mutation.
- `beginMachine` returns invalid empty state for invalid programs. `machineInput` appends
  bounded unread input and compacts consumed values. Output is bounded to 4096 words.
  Drain output only through an authoritative host transaction; output alone is not a reward.
- `runMachine(program,state,budget)` executes 1..1000000 instructions per call, with an
  absolute 1e9-step ceiling. BudgetExceeded means suspended, not an invalid puzzle.
  WaitingInput and OutputFull consume no instruction. Yield explicitly suspends after
  consuming one instruction. Arithmetic/address faults keep the offending PC/operands.
- Programs cannot access clocks, RNG, filesystem, network or host memory. Host adapters
  validate player ownership and permitted puzzle inputs before invoking the machine.
  Completion criteria compare actual output/state; do not trust a client success flag.
- Link `AIVERSE::strategy`; persist MachineState through `strategy/Persistence.h` in
  NativeState, including queues, input cursor, registers, memory, PC, steps and fault.
  Bind program revision/module VERSION. Never keep a mutable VM on NativeModule.
- Follow `examples/native/strategy` VERSION 4, command 13, one instruction per sim tick.
  Run strategy tests and `python3 tests/libraries/strategy_native.py`: restore after Read,
  resume multiplication/output, verify bounded loops, overflow, blocked IO and memory faults.

This is an integer register-machine puzzle model. It is not a Lua replacement, native-code
sandbox, concurrent circuit simulator, program editor or complete Zachtronics game.
