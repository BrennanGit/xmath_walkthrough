// Copyright 2022-2023 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#include "common.h"

/**
 * The box filter coefficient array.
 */
extern
const double filter_coef[TAP_COUNT];


//// +rx_frame
// Accept a frame of new audio data 
static inline 
void rx_frame(
    double buff[],
    const chanend_t c_audio)
{    
  // The exponent associated with the input samples
  const exponent_t input_exp = -31;

  for(int k = 0; k < FRAME_SIZE; k++){
    // Read PCM sample from channel
    const int32_t sample_in = (int32_t) chan_in_word(c_audio);
    // Convert PCM sample to floating-point
    const double samp_f = ldexp(sample_in, input_exp);
    // Place at beginning of history buffer in reverse order (to match the
    // order of filter coefficients).
    buff[FRAME_SIZE-k-1] = samp_f;
  }

  timer_start(TIMING_FRAME);
}
//// -rx_frame


//// +tx_frame
// Send a frame of new audio data
static inline 
void tx_frame(
    const chanend_t c_audio,
    const double buff[])
{    
  // The exponent associated with the output samples
  const exponent_t output_exp = -31;

  timer_stop(TIMING_FRAME);

  // Send FRAME_SIZE new output samples at the end of each frame.
  for(int k = 0; k < FRAME_SIZE; k++){
    // Get double sample from frame output buffer (in forward order)
    const double samp_f = buff[k];
    // Convert double sample back to PCM using the output exponent.
    const q1_31 sample_out = round(ldexp(samp_f, -output_exp));
    // Put PCM sample in output channel
    chan_out_word(c_audio, sample_out);
  }
}
//// -tx_frame


//// +filter_sample
//Apply the filter to produce a single output sample
double filter_sample(
    const double sample_history[TAP_COUNT])
{
  // Compute the inner product of sample_history[] and filter_coef[]
  double acc = 0.0;
  for(int k = 0; k < TAP_COUNT; k++)
    acc += sample_history[k] * filter_coef[k];
  return acc;
}
//// -filter_sample


//// +filter_task
/**
 * This is the thread entry point for the hardware thread which will actually 
 * be applying the FIR filter.
 * 
 * `c_audio` is the channel over which PCM audio data is exchanged with tile[0].
 */
void filter_task(
    chanend_t c_audio)
{
  // History of received input samples, stored in reverse-chronological order
  double sample_history[HISTORY_SIZE] = {0};

  // Buffer used to hold output samples
  double frame_output[FRAME_SIZE] = {0};

  // Loop forever
  while(1) {

    // Read in a new frame
    rx_frame(&sample_history[0], 
             c_audio);

    // Calc output frame
    for(int s = 0; s < FRAME_SIZE; s++){
      timer_start(TIMING_SAMPLE);
      frame_output[s] = filter_sample(&sample_history[FRAME_SIZE-s-1]);
      timer_stop(TIMING_SAMPLE);
    }

    // Make room for new samples at the front of the vector
    memmove(&sample_history[FRAME_SIZE], 
            &sample_history[0], 
            TAP_COUNT * sizeof(double));

    // Send out the processed frame
    tx_frame(c_audio, 
             &frame_output[0]);
  }
}
//// -filter_task