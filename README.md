# bip-buffer

A bipartite circular buffer in C for high-performance, zero-copy buffering.

## Why a bip buffer?

A traditional ring buffer can return data split across the wrap boundary,
forcing callers to perform two reads or copy into a contiguous scratch buffer.
A bip buffer guarantees that both reads (`peek`) and writes (`reserve`) always
return a single contiguous pointer, eliminating that overhead.

This makes bip buffers well suited for:

- Network I/O and protocol framing where `recv`/`send` need contiguous spans
- Audio/video pipelines that hand buffers to DMA or codec APIs
- Any producer-consumer path where avoiding `memcpy` matters

## How it works

A bip buffer maintains a flat byte array divided into two committed regions:

- **Region A** -- the oldest data, always read first via `peek`/`consume`
- **Region B** -- wraps to the start of the array when more contiguous space
  is available there than after A

Writes go through a two-phase protocol: `reserve` obtains a contiguous writable
pointer, then `commit` makes those bytes readable. This guarantees that both
reads and writes always return a single contiguous span.

### Memory layout walkthrough

A 4096-byte buffer through a full write-read-wrap cycle (each column = 256 bytes):

**1. `new_bip_buffer(4096)`**

```mermaid
packet-beta
  0-15: "free (4096)"
```

**2. `reserve(3072)` + `commit(3072)` -- data written to Region A**

```mermaid
packet-beta
  0-11: "Region A (3072)"
  12-15: "free (1024)"
```

**3. `consume(2048)` -- oldest 2048 bytes discarded, A shrinks**

```mermaid
packet-beta
  0-7: "free (2048)"
  8-11: "A (1024)"
  12-15: "free (1024)"
```

**4. `reserve(1024)` -- 2048 free before A > 1024 free after A, wraps to front**

```mermaid
packet-beta
  0-3: "reserved (1024)"
  4-7: "free (1024)"
  8-11: "A (1024)"
  12-15: "gap (1024)"
```

**5. `commit(1024)` -- reservation becomes Region B**

```mermaid
packet-beta
  0-3: "B (1024)"
  4-7: "free (1024)"
  8-11: "A (1024)"
  12-15: "gap (1024)"
```

`peek` returns Region A -- oldest data is always read first.

**6. `consume(1024)` -- A fully consumed, B promoted to A**

```mermaid
packet-beta
  0-3: "A (1024)"
  4-15: "free (3072)"
```

In step 4 the 1024 bytes after A become temporarily unusable because the
reservation wraps to the front where more contiguous space is available.
That space is reclaimed once A is fully consumed (step 6). This trade-off --
a small amount of temporarily wasted space -- is what guarantees contiguous
reads and writes without any copying.

### Buffer state transitions

| State | Operation | Next State |
|-------|-----------|------------|
| Empty | reserve + commit | A |
| A | reserve + commit (append) | A |
| A | partial consume | A |
| A | reserve wraps + commit | A+B |
| A | consume all | Empty |
| A | clear | Empty |
| A+B | reserve + commit (append B) | A+B |
| A+B | partial consume A | A+B |
| A+B | consume all of A | A (B promoted) |
| A+B | clear | Empty |

`delete_bip_buffer` can be called from any state.

## API

| Function | Description |
|----------|-------------|
| `new_bip_buffer(size)` | Allocate a buffer with `size` bytes of capacity. |
| `delete_bip_buffer(&bb)` | Free all memory and set the pointer to NULL. |
| `bb->reserve(bb, n, &got)` | Reserve up to `n` writable bytes (clamped to available space). |
| `bb->commit(bb, n)` | Commit `n` reserved bytes (0 cancels the reservation). |
| `bb->peek(bb, &size)` | Get a pointer to the oldest committed data. |
| `bb->consume(bb, n)` | Discard `n` committed bytes from the read side. |
| `bb->clear(bb)` | Reset to empty; backing memory is retained. |
| `bb->get_committed_size(bb)` | Total readable bytes (region A + B). |
| `bb->get_reservation_size(bb)` | Bytes in the active reservation. |
| `bb->get_buffer_size(bb)` | Total capacity in bytes. |

## Usage

### Basic write and read

```c
#include "bip_buffer.h"
#include <string.h>

bip_buffer_t *bb = new_bip_buffer(4096);

/* Write */
size_t reserved;
uint8_t *wr = bb->reserve(bb, payload_len, &reserved);
if (wr) {
    memcpy(wr, payload, reserved);
    bb->commit(bb, reserved);
}

/* Read */
size_t available;
uint8_t *rd = bb->peek(bb, &available);
if (rd) {
    process(rd, available);
    bb->consume(bb, available);
}

delete_bip_buffer(&bb);   /* bb is set to NULL */
```

### Handling partial reservations

`reserve` may return fewer bytes than requested when the buffer is nearly full.
Always use the `reserved` output to know how much space you actually got:

```c
size_t reserved;
uint8_t *wr = bb->reserve(bb, 1024, &reserved);
if (wr == NULL) {
    /* Buffer is completely full */
    return -1;
}

/* reserved may be less than 1024 — only write that many bytes */
size_t to_send = (msg_len < reserved) ? msg_len : reserved;
memcpy(wr, msg, to_send);
bb->commit(bb, to_send);
```

### Streaming producer-consumer loop

```c
bip_buffer_t *bb = new_bip_buffer(8192);

/* Producer: feed data in chunks */
while ((n = recv(fd, tmp, sizeof(tmp), 0)) > 0) {
    size_t reserved;
    uint8_t *wr = bb->reserve(bb, (size_t)n, &reserved);
    if (wr == NULL) {
        break;  /* buffer full — consumer must drain */
    }
    memcpy(wr, tmp, reserved);
    bb->commit(bb, reserved);
}

/* Consumer: drain all committed data */
for (;;) {
    size_t available;
    uint8_t *rd = bb->peek(bb, &available);
    if (rd == NULL) {
        break;  /* empty */
    }
    ssize_t sent = send(fd, rd, available, 0);
    if (sent > 0) {
        bb->consume(bb, (size_t)sent);
    }
}

delete_bip_buffer(&bb);
```

### Cancelling a reservation

Committing 0 bytes discards the reservation without writing anything:

```c
size_t reserved;
uint8_t *wr = bb->reserve(bb, 256, &reserved);
/* ... decide not to write ... */
bb->commit(bb, 0);  /* cancel — no data committed */
```

### Error handling

`new_bip_buffer` returns NULL on allocation failure:

```c
bip_buffer_t *bb = new_bip_buffer(size);
if (bb == NULL) {
    /* calloc failed for the struct or the backing buffer */
    return -ENOMEM;
}
```

`delete_bip_buffer` is safe to call when the pointer is already NULL:

```c
bip_buffer_t *bb = NULL;
delete_bip_buffer(&bb);   /* no-op */
```

## Thread safety

This implementation is **not** thread-safe. If you share a bip buffer between
threads you must provide your own synchronisation (mutex, spinlock, etc.).

## Building

All build commands run inside a Podman container. The only host dependency
is `podman` (installed automatically by `make install-deps`).

```sh
make image-build    # build the dev container (once)
make test           # run all unit tests
make gcov           # run tests + generate coverage report
make lint           # clang-tidy + cppcheck + lizard
make release        # build native static library  -> build/release/libbip_buffer.a
make release-all    # cross-compile for all architectures
make examples       # build and run examples/
make all            # format + compile-db + lint + test + gcov + release + examples
make clean          # remove build/ and dist/
```

To run tools directly on the host (skip container):

```sh
make test SKIP_CONTAINER=1
```

### Cross-compilation targets

Cross-compilation uses Ceedling with clang-20 mixins (`--target` + `--sysroot`).
Output goes to `dist/<target>/libbip_buffer.a`.

| Target | Triple |
|--------|--------|
| `release-arm32` | `arm-linux-gnueabihf` |
| `release-arm64` | `aarch64-linux-gnu` |
| `release-x86_64` | `x86_64-linux-gnu` |
| `release-mips` | `mips-linux-gnu` |
| `release-mips64` | `mips64-linux-gnuabi64` |
| `release-riscv64` | `riscv64-linux-gnu` |
| `release-powerpc` | `powerpc-linux-gnu` |
| `release-freebsd-x86_64` | `x86_64-unknown-freebsd14` |
| `release-freebsd-arm64` | `aarch64-unknown-freebsd14` |

## Using with CMake

The library is a single translation unit — you can integrate it directly or
pull it as an external dependency.

### Direct compilation (no Ceedling required)

The simplest approach — add the source file to your CMake project:

```cmake
add_library(bip_buffer STATIC)
target_sources(bip_buffer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bip-buffer/src/bip_buffer.c)
target_include_directories(bip_buffer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bip-buffer/include)
target_compile_features(bip_buffer PRIVATE c_std_11)

target_link_libraries(my_app PRIVATE bip_buffer)
```

### FetchContent (automatic download)

```cmake
include(FetchContent)

FetchContent_Declare(
    bip_buffer
    GIT_REPOSITORY https://github.com/silkrad/bip-buffer.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(bip_buffer)

# bip-buffer has no CMakeLists.txt, so define the target manually
if (NOT TARGET bip_buffer)
    add_library(bip_buffer STATIC)
    target_sources(bip_buffer PRIVATE ${bip_buffer_SOURCE_DIR}/src/bip_buffer.c)
    target_include_directories(bip_buffer PUBLIC ${bip_buffer_SOURCE_DIR}/include)
    target_compile_features(bip_buffer PRIVATE c_std_11)
endif()

target_link_libraries(my_app PRIVATE bip_buffer)
```

### ExternalProject (builds with Ceedling)

Use this if you want the library built with the same flags and toolchain
as the project's own release build. Requires Ceedling on the build host.

```cmake
include(ExternalProject)

ExternalProject_Add(
    bip_buffer_ext
    GIT_REPOSITORY https://github.com/silkrad/bip-buffer.git
    GIT_TAG        main
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ceedling release
    BUILD_IN_SOURCE   TRUE
    INSTALL_COMMAND   ""
)

ExternalProject_Get_Property(bip_buffer_ext SOURCE_DIR)

add_library(bip_buffer STATIC IMPORTED GLOBAL)
set_target_properties(bip_buffer PROPERTIES
    IMPORTED_LOCATION ${SOURCE_DIR}/build/release/libbip_buffer.a
    INTERFACE_INCLUDE_DIRECTORIES ${SOURCE_DIR}/include
)
add_dependencies(bip_buffer bip_buffer_ext)

target_link_libraries(my_app PRIVATE bip_buffer)
```

### Using the pre-built static library

If you've already built `libbip_buffer.a` (via `make release` or
`make release-arm64`, etc.), point CMake at the artifact directly:

```cmake
add_library(bip_buffer STATIC IMPORTED)
set_target_properties(bip_buffer PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libbip_buffer.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(my_app PRIVATE bip_buffer)
```

## Project layout

```
include/
  bip_buffer.h          Public header
src/
  bip_buffer.c          Implementation (single translation unit)
test/
  test_bip_buffer.c     Unity tests (29 tests, 100% line coverage)
  support/
    mock_calloc.c/h     --wrap=calloc helper for allocation failure tests
examples/
  basic.c             Basic write, read, partial reserve, cancel
  wrap.c              Wrap-around behavior and Region B promotion
mixins/
  <arch>.yml            Ceedling cross-compilation mixins (--target, --sysroot, -fPIC)
containers/
  build.Containerfile   Dev container (clang-20, ceedling, lint tools, cross-compilers)
project.yml             Ceedling configuration
Makefile                Build orchestrator
```

## License

Apache 2.0 -- see [LICENSE](LICENSE) for details.

## Acknowledgements

Based on [Simon Cooke's bip buffer concept](https://www.codeproject.com/Articles/3479/The-Bip-Buffer-The-Circular-Buffer-with-a-Twist).
