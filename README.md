# FTLCheck an” SSD Flash Translation Layer + Validation Framework

Two things in one C++17 codebase, mirroring how SSD firmware is
actually built and tested:

1. **The firmware side** â€” a working **Flash Translation Layer (FTL)**
   over a modeled raw NAND chip: logicalâ†’physical page mapping,
   **garbage collection**, **wear leveling**, **bad-block management**,
   and **power-loss recovery** from on-flash OOB metadata.
2. **The validation side** â€” a from-scratch **test framework**
   (factory-registered test cases, singleton runner, fixtures,
   assertion macros) with **fault injection**: power loss mid-write,
   bit flips, erase failures â€” plus console results and a
   self-contained **HTML report**.

23 tests across 5 suites, all passing. ~1,700 lines. No dependencies.

## Build & run

Needs g++ on PATH (any GCC toolchain works, e.g. w64devkit or MinGW on Windows).

```
build.bat                                   (or: make)
bin\ftlcheck.exe demo                       narrated walkthrough of the FTL
bin\ftlcheck.exe list                       all registered tests
bin\ftlcheck.exe run                        run everything
bin\ftlcheck.exe run --suite power_loss     one suite
bin\ftlcheck.exe run --report report.html   + HTML report (open in browser)
make memguard                               build with MemGuard (../memguard)
                                            as the global C++ allocator
```

## Why NAND flash needs an FTL (30-second version)

NAND flash has brutal rules: write one **page** at a time, erase only a
whole **block**, never overwrite a programmed page, and every block
dies after ~thousands of erases. The FTL makes that mess look like a
normal overwritable disk â€” it is the heart of every SSD's firmware.

## Architecture

```
                 host reads/writes logical pages
                              |
 +----------------------------v-----------------------------+
 |  Ftl                                             ftl/     |
 |   map: LPN -> (block, page)      per-block valid counts   |
 |   write = program new copy -> remap -> invalidate old     |
 |   GC: victim = fewest-valid FULL block (tie: least worn)  |
 |   wear leveling: new blocks = least-erased free block     |
 |   recover(): rescan every OOB, highest seq wins           |
 +----------------------------v-----------------------------+
 |  NandDevice                                      nand/    |
 |   blocks x pages (+OOB: lpn, seq, checksum per page)      |
 |   enforces: sequential program, erase-before-write,       |
 |   erase limits -> grown bad blocks                        |
 |   fault injection: power loss / bit flip / erase failure  |
 +-----------------------------------------------------------+

 validation framework                          framework/
   TestCase (abstract) <- FtlFixture <- 23 concrete tests
   TestRegistry (singleton, factory registration via FTL_TEST macro)
   TestRunner  (singleton: setUp -> run -> tearDown, timing, catching)
   report: console summary + self-contained HTML
```

### UML (class diagram)

```
 +----------------+       registers        +---------------------+
 |   TestCase     |<----------------------+|  TestRegistry (S)   |
 |  (abstract)    |   factory lambdas      |  vector<TestInfo>   |
 |----------------|                        +---------------------+
 | +setUp()       |                                   ^ reads
 | +tearDown()    |                        +---------------------+
 | +run() = 0     |                        |  TestRunner (S)     |
 +-------^--------+                        |  run(filters)       |
         | inherits                        +---------------------+
 +-------+--------+                                  | produces
 |  FtlFixture    |  owns  +-----------+   +---------v---------+
 |  dev, ftl,     +------->|    Ftl    |   |    TestResult     |
 |  helpers       |        +-----+-----+   +-------------------+
 +-------^--------+              | drives
         | inherits (x23)  +-----v-------+
 +-------+--------+        | NandDevice  |     (S) = singleton
 | concrete tests |        +-------------+
 +----------------+
```

## The two bugs the framework caught in its own FTL (true story)

1. **Wear-leveling starvation**: GC victims tied at 0 valid pages were
   picked by lowest index â€” so blocks 0â€“2 absorbed ALL erases (9/9/8)
   while 13 blocks sat at zero. Fix: break ties by erase count. The
   demo's wear table now shows a 1â€“2 erase spread across all 16 blocks.
2. **Free-pool bleed on grown-bad blocks**: when a GC victim failed its
   erase, that round reclaimed nothing and the free pool shrank until a
   later write hit "no free block available". Fix: GC loops until a
   block is genuinely reclaimed. Caught by the erase-failure injection
   test.

Both are exactly the class of bug SSD firmware teams hunt â€” which is
the point of building the validator alongside the firmware.

## Design decisions I can defend

- **Page-level mapping** (vs block-level): more RAM, far less write
  amplification; real SSDs use hybrids.
- **Checksum-in-OOB, detect-only**: stands in for ECC. Real NAND uses
  BCH/LDPC codes that also correct; detection is the honest MVP.
- **Greedy GC victim selection** (fewest valid, then least worn).
- **Overprovisioning (4 of 16 blocks)**: guarantees GC always has
  headroom â€” why a "256 GB" SSD contains more than 256 GB of flash.
- **Recovery seals partial blocks as Full** rather than resuming them:
  simple and safe; GC reclaims the tail later.

## Limitations (known, deliberate)

- Detection without correction (no real ECC).
- No static wear leveling (cold blocks are not proactively rotated) â€”
  I can explain how I'd add it.
- Map lives fully in RAM and is rebuilt by full scan; real drives
  checkpoint the map to flash to speed up boot.
- Single-threaded; real firmware pipelines NAND operations.

