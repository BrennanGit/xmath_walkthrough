
#include "common.h"

/**
 * The box filter coefficient array.
 */
extern 
const q2_30 filter_coef[TAP_COUNT];

/**
 * The exponent associatd with the filter coefficients.
 */
const exponent_t coef_exp = -30;

/**
 * The exponent associated with the output signal.
 */
const exponent_t output_exp = -31;


/**
 * Apply the filter to produce a single output sample.
 * 
 * `sample_history[]` contains the `TAP_COUNT` most-recent input samples, with
 * the newest samples first (reverse time order).
 * 
 * `history_exp` is the exponent associated with the samples in 
 * `sample_history[]`.
 * 
 * `history_hr` is the headroom present in `sample_history[]`.
 */
q1_31 filter_sample(
    const int32_t sample_history[TAP_COUNT],
    const exponent_t history_exp,
    const headroom_t history_hr)
{
  // The accumulator into which partial results are added.
  float_s64_t acc;
  acc.mant = 0;

  // The headroom of the filter_coef[] vector. Used by vect_s32_dot_prepare().
  const headroom_t coef_hr = HR_S32(filter_coef[0]);

  // Determine the accumulator exponent and the shift values required by
  // vect_s32_dot().
  right_shift_t b_shr, c_shr;
  vect_s32_dot_prepare(&acc.exp, &b_shr, &c_shr, 
                        history_exp, coef_exp,
                        history_hr, coef_hr, TAP_COUNT);

  // Compute the inner product's mantissa using the given shift parameters.
  acc.mant = vect_s32_dot(&sample_history[0], &filter_coef[0], TAP_COUNT, 
                        b_shr, c_shr);

  // Convert the result to a fixed-point value using the output exponent
  q1_31 sample_out = float_s64_to_fixed(acc, output_exp);

  return sample_out;
}

/**
 * Apply the filter to a frame with `FRAME_SIZE` new input samples, producing
 * one output sample for each new sample.
 * 
 * Computed output samples are placed into `frame_out[]` with the oldest samples
 * first (forward time order).
 * 
 * `history_in[]` contains the most recent `HISTORY_SIZE` samples with the newest
 * samples first (reverse time order). The first `FRAME_SIZE` samples of 
 * `history_in[]` are new.
 * 
 * `history_in_exp` is the block floating-point exponent associated with the
 * samples in `history_in[]`.
 * 
 * `history_in_hr` is the headroom of the `history_in[]` vector.
 */
void filter_frame(
    q1_31 frame_out[FRAME_SIZE],
    const int32_t history_in[HISTORY_SIZE],
    const exponent_t history_in_exp,
    const headroom_t history_in_hr)
{
  // Compute FRAME_SIZE output samples.
  for(int s = 0; s < FRAME_SIZE; s++){
    timer_start();
    frame_out[s] = filter_sample(&history_in[FRAME_SIZE-s-1], 
                                  history_in_exp, 
                                  history_in_hr);
    timer_stop();
  }
}

/**
 * This is the thread entry point for the hardware thread which will actually 
 * be applying the FIR filter.
 * 
 * `c_audio` is the channel over which PCM audio data is exchanged with tile[0].
 */
void filter_task(
    chanend_t c_audio)
{
  // Buffer used for storing input sample history.
  int32_t sample_history[HISTORY_SIZE] = {0};
  // Exponent associated with sample_history[]
  exponent_t sample_history_exp;
  // Headroom associated with sample_history[]
  headroom_t sample_history_hr;

  // Buffer used to hold output samples.
  q1_31 frame_output[FRAME_SIZE] = {0};

  // Loop forever
  while(1) {
    // Read in a new frame. It is placed in reverse order at the beginning of
    // sample_history[]
    read_frame(&sample_history[0], c_audio, FRAME_SIZE);

    // For now, the exponent associated with each new input frame is -31.
    sample_history_exp = -31;

    // Compute headroom of input frame
    sample_history_hr = vect_s32_headroom(&sample_history[0], HISTORY_SIZE);

    // Compute FRAME_SIZE output samples.
    for(int s = 0; s < FRAME_SIZE; s++){
      timer_start();
      frame_output[s] = filter_sample(&sample_history[FRAME_SIZE-s-1], 
                                      sample_history_exp, 
                                      sample_history_hr);
      timer_stop();
    }

    // Send out the processed frame
    send_frame(c_audio, &frame_output[0], FRAME_SIZE);

    // Finally, shift the sample_history[] buffer up FRAME_SIZE samples.
    // This is required to maintain ordering of the sample history.
    memmove(&sample_history[FRAME_SIZE], &sample_history[0], 
            TAP_COUNT * sizeof(int32_t));
  }
}
