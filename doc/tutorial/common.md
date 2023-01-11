
# Common Code

Before diving into stage-specific code, it is worth taking a brief look at some
of the common code.

All code common to all parts and stages can be found in the
`src/common/` directory. However, note that each stage uses only _one_
of the source files in `src/common/filters/`.

Note that each stage of this tutorial will assume that the user has read the
previous parts. This will help avoid reiterating information.

## `main.xc`

`main.xc`, the firmware application's entry point. `main()` is defined in an XC
file rather than a C file to make use of the XC language's convenient syntax for
allocating channel resources and for bootstrapping the threads that will be
running on each tile.

```{literalinclude} ../../src/common/main.xc
---
language: C
start-after: +main
end-before: -main
---
```

One thread is spawned on `tile[0]`, `wav_io_task()`, which handles all file IO
and breaking the input and output signal up into frames of audio.

On the second tile, `tile[1]` two more threads are spawned. The filter thread,
with `filter_task()` as an entry point, is where the actual filtering takes
place. It communicates with `wav_io_task` across tiles using a channel resource.

Also on `tile[1]` is `timer_report_task()`. This is a trivial thread which
simply waits until the application is almost ready to terminate, and then
reports some timing information (collected by `filter_task`) back to
`wav_io_task` where the performance info can be written out to a text file.

The macros `APP_NAME`, `INPUT_WAV`, `OUTPUT_WAV` and `OUTPUT_JSON` are all
defined it the particular stage's `CMakeLists.txt`.

## `common.h`

The `common.h` header includes several other boilerplate headers, and
then defines several macros required by most stages.

From `common.h`:
```c
#define TAP_COUNT     (1024)
#define FRAME_SIZE    (256)
#define HISTORY_SIZE  (TAP_COUNT + FRAME_SIZE)
```

`TAP_COUNT` is the order of the digital FIR filter being implemented. It is the
number of filter coefficients. It is the number of multiplications and additions
necessary (arithmetically speaking) to compute the filter output. And it is also
the minimum input sample history size needed to compute the filter output for a
particular time step.

Rather than `filter_task` receiving input samples one-by-one, it receives new
input samples in frames. Each frame of input samples will contain `FRAME_SIZE`
new input sample values.

Because the filter thread is receiving input samples in batches of `FRAME_SIZE`
and sending output samples in batches of `FRAME_SIZE`, it also makes sense to
process them as a batch. Additionally, when processing a batch, it is more
convenient and more efficient to store the sample history in a single linear
buffer. That buffer must then be `HISTORY_SIZE` elements long.

## `misc_func.h`

The `misc_func.h` header contains several simple inline scalar functions
required in various places throughout the tutorial. Future versions of
`lib_xcore_math` will likely contain implementations of these functions, but for
now our application has to define them.

## `filters/`

The `src/common/filters/` directory contains 4 source files, each of
which defines the same filter coefficients, but in different formats. Each
stage's firmware uses only _one_ of these filters.

### `filter_coef_double.c` 

`filter_coef_double.c` contains the coefficients as an array of `double`
elements:

```C
const double filter_coef[TAP_COUNT] = {...};
```

### `filter_coef_float.c` 

`filter_coef_float.c` contains the coefficients as an array of `float` elements:

```C
const float filter_coef[TAP_COUNT] = {...};
```

### `filter_coef_q2_30.c` 

`filter_coef_q2_30.c` contains the coefficients as an array of `q2_30` elements. `q2_30` is a fixed-point type defined in `lib_xcore_math`. It will be described in more detail later.

```C
const q2_30 filter_coef[TAP_COUNT] = {...};
```

### `filter_coef_q4_28.c` 

`filter_coef_q4_28.c` contains the coefficients as an array of `q4_28` elements. `q4_28` is a fixed-point type defined in `lib_xcore_math`. It will be described in more detail later.

```C
const q4_28 filter_coef[TAP_COUNT] = {...};
```

## Timing

Filter (time) performance is measured through the timing module in `timing.h`. The implementation of `timing.c` is not important. What you will find in each stage's implementation are the following four calls:

```c
timer_start(TIMING_SAMPLE);
```
```c
timer_stop(TIMING_SAMPLE);
```
```c
timer_start(TIMING_FRAME);
```
```c
timer_stop(TIMING_FRAME);
```

You'll find the pair `timer_start(TIMING_SAMPLE)` and `timer_stop(TIMING_SAMPLE)` surrounding the calls to `filter_sample()` (more on `filter_sample()` later).

They are how the application (and all subsequent stages) measures the per-sample
filter performance. The device's 100 MHz reference clock is used to capture
timestamps immediately before and after the new output sample is calculated
(when `TIMING_SAMPLE` is used). This tells us how many 100 MHz clock ticks
elapsed while computing the output sample. The code in `timing.c` ultimately
just computes the average time elapsed across all output samples.

Because the per-sample timing misses a lot of the required processing, in each
stage you will also find calls to `timer_start(TIMING_FRAME)` and
`timer_stop(TIMING_FRAME)` in `rx_frame()` and `tx_frame()` respectively. This
pair (when `TIMING_FRAME` is used) measure the time taken to process the entire
frame.
