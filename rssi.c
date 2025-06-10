/*
 * Copyright (C) 2011 Jamey Sharp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <fftw3.h>
#include <math.h>
#include <portaudio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "prn.h"
#include <signal.h>

static void complex_mulf(fftwf_complex to, fftwf_complex a, fftwf_complex b)
{
        float real = a[0] * b[0] - a[1] * b[1];
        float imag = a[1] * b[0] + a[0] * b[1];
        to[0] = real;
        to[1] = imag;
}

static void complex_conj_mulf(fftwf_complex to, fftwf_complex a, fftwf_complex b)
{
        float real = a[0] * b[0] + a[1] * b[1];
        float imag = a[1] * b[0] - a[0] * b[1];
        to[0] = real;
        to[1] = imag;
}


#define PULSEAUDIO 1
#define TRACE 0
//#define DECIMATE 2

struct signal_strength {
	float snr;
	float doppler;
	float phase;
};

static unsigned int read_samples(fftwf_complex *data, unsigned int data_len)
{
#if 0
	unsigned int i = 0;
	for(i = 0; i < data_len; ++i)
	{
		float buf[2];
		if(fread(buf, sizeof(float), 2, stdin) != 2)
			break;
		data[i][0] = buf[0];
		data[i][1] = buf[1];
	}
	return i;
#else
        int n = fread(data, 2*sizeof(float), data_len, stdin);
        return n;
#endif
}

static void update_stats(struct signal_strength *stats, double bin_width, int shift, float phase, float snr_0, float snr_1, float snr_2)
{
	float shift_correction;
	/* ignore this sample if it is not a local peak */
	if(snr_0 > snr_1 || snr_2 > snr_1)
		return;
	/* take only the highest peak */
	if(snr_1 <= stats->snr)
		return;

	/* do a weighted average of the three points around this peak */
	shift_correction = (snr_2 - snr_0) / (snr_0 + snr_1 + snr_2);

	stats->snr = snr_1;
	stats->doppler = (shift + shift_correction) * bin_width;
	stats->phase = phase;
}

#if PULSEAUDIO
#define AUDIO_SAMPLE_RATE 8000
typedef struct {
    float counter;
    float snr;
} Context;

static int pa_cb(const void *input, void *output,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo * timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
{
    float * out = output;
    Context * s = userData;

    for (int i = 0; i < frameCount; i++) {
        out[i] = sin(s->counter);
        s->counter += s->snr / AUDIO_SAMPLE_RATE;
    }
    return paContinue;
}
#endif

static int g_run = 1;

static void signal_handler(int sig)
{
    g_run = 0;
}

int main(int argc, char **argv)
{
	if(argc <= 3)
	{
		fprintf(stderr, "usage: %s sample-freq carrier-offset target-sv\n", argv[0]);
		exit(1);
	}

        signal(SIGINT, signal_handler);

	const unsigned int sample_freq = atoi(argv[1]);
	const unsigned int carrier_offset = atoi(argv[2]);
	const unsigned int target_sv = atoi(argv[3]); /* 0 (scan everything), 1 .. 32 */
	if (target_sv > MAX_SV) {
            printf("invalid sv\n");
            return -1;
	}

        uint8_t scan_sv[MAX_SV] = {0};
        if (target_sv) {
            scan_sv[target_sv - 1] = 1;
        } else { /* scan everything */
            for (int i = 0; i < MAX_SV; i++)
                scan_sv[i] = 1;
        }

#if PULSEAUDIO
        PaError err;
        err = Pa_Initialize();
        if (err != paNoError) {
            printf("Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
            return -1;
        }

        Context context;
        context.counter = 0;
        context.snr = 0;

        PaStream * stream;
        err = Pa_OpenDefaultStream(&stream, 0, 1, paFloat32, AUDIO_SAMPLE_RATE, 64, pa_cb, &context);
        if (err != paNoError) {
            printf("Pa_OpenDefaultStream failed: %s\n", Pa_GetErrorText(err));
            Pa_Terminate();
            return -1;
        }

        Pa_StartStream(stream);
#endif

	unsigned int training_len = sample_freq * 20 / 1000;
	fftwf_complex *training = fftw_malloc(sizeof(fftwf_complex) * training_len);
	struct signal_strength signals[MAX_SV];
	fftwf_plan training_plan = fftwf_plan_dft_1d(training_len, training, training, FFTW_FORWARD, FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

	const unsigned int len = sample_freq / 1000;
	const unsigned int fft_len = len / 2 + 1;
	fftwf_complex *prod = fftw_malloc(sizeof(fftwf_complex) * len);

        /* build ca_fft buffers */

	fftwf_complex *ca_fft[MAX_SV];

        for (int i = 0 ; i < MAX_SV; i++) {
                if (scan_sv[i]) {
            int sv = i + 1;

	    void *ca_buf = fftw_malloc(sizeof(fftwf_complex) * fft_len);
	    float *ca_samples = ca_buf;
	    ca_fft[i] = ca_buf;

	    fftwf_plan fft = fftwf_plan_dft_r2c_1d(len, ca_samples, ca_fft[i], FFTW_ESTIMATE | FFTW_DESTROY_INPUT);

	/* I think each forward FFT and the inverse FFT multiply by
	 * another sqrt(len), so to get normalized power, we need to
	 * divide by sqrt(len)^3. This doesn't change any of the
	 * results, except when debugging the raw per-bin power. For the
	 * normalization convention FFTW uses see
	 * http://www.fftw.org/doc/The-1d-Discrete-Fourier-Transform-_0028DFT_0029.html
	 */
	    const double samples_per_chip = sample_freq / 1023e3;
	    const double normalize_dft = pow(len, 1.5);

	    for(unsigned int i = 0; i < len; ++i)
	    	ca_samples[i] = (cacode((int) (i / samples_per_chip), sv) ? 1 : -1) / normalize_dft;

	    fftwf_execute(fft);

	    fftwf_destroy_plan(fft);
                }
        }

        fftwf_plan ifft = fftwf_plan_dft_1d(len, prod, prod, FFTW_BACKWARD, FFTW_ESTIMATE | FFTW_DESTROY_INPUT);


#if defined(DECIMATE)
        int dec = 0;
#endif
while(g_run) {
	if(read_samples(training, training_len) < training_len)
	{
		fprintf(stderr, "couldn't read %u input samples needed for training\n", training_len);
		exit(EXIT_FAILURE);
	}

#if defined(DECIMATE)
        dec++;
        if (dec < DECIMATE)
            continue;
        dec = 0;
#endif

	fftwf_execute(training_plan);

	for(int i = 0; i < training_len; ++i)
	{
		/* precompute the complex conjugate of the training FFT */
		training[i][1] = -training[i][1];
	}

	for(int i = 0; i < MAX_SV; ++i)
                if (scan_sv[i]) {
        fftwf_complex *data_fft = training;
        int data_fft_len = training_len;
        int sv = i + 1;

	struct signal_strength stats;
        const long shift_hz = 10000;
        const long shift_inc = 2;

	const int max_shift = shift_hz * (long)data_fft_len / sample_freq;
	const double bin_width = (double) sample_freq / data_fft_len;
	float snr_1 = 0, snr_2 = 0, best_phase_1 = 0;
	float max_pwr, best_phase;
	int shift;


	if(TRACE)
		printf("# SV %d correlation\n", sv);
	stats.snr = 0;
	for(shift = -max_shift; shift <= max_shift; shift += shift_inc)
	{
		const float doppler = (carrier_offset + shift) * bin_width;
		float tot_pwr = 0, snr;
		for(unsigned int i = 0; i < len / 2; ++i)
		{
			complex_mulf(prod[i], data_fft[(i * (data_fft_len / len) + shift + data_fft_len) % data_fft_len], ca_fft[sv - 1][i]);
			complex_conj_mulf(prod[len - 1 - i], data_fft[((len - 1 - i) * (data_fft_len / len) + shift + data_fft_len) % data_fft_len], ca_fft[sv - 1][i + 1]);
		}

		fftwf_execute(ifft);

		max_pwr = best_phase = 0;
		for(unsigned int i = 0; i < len; ++i)
		{
			float pwr = prod[i][0] * prod[i][0] + prod[i][1] * prod[i][1];
			float phase = i * (1023.0 / len);
			if(TRACE)
				printf("%f\t%f\t%f\n", doppler, phase, pwr);
			if(pwr > max_pwr)
			{
				max_pwr = pwr;
				best_phase = phase;
			}
			tot_pwr += pwr;
		}

		snr = max_pwr / (tot_pwr / len);
		update_stats(&stats, bin_width, shift - 1, best_phase_1, snr_2, snr_1, snr);
		if(TRACE)
			printf("# best for doppler %f: code phase %f, S/N %f\n", doppler, best_phase, snr);

		snr_2 = snr_1;
		snr_1 = snr;
		best_phase_1 = best_phase;
	}
	update_stats(&stats, bin_width, max_shift, best_phase_1, snr_2, snr_1, 0);
	if(TRACE)
		printf("\n");

        signals[i] = stats;
                }

#if 1
	for(unsigned int i = 0; i < MAX_SV; ++i)
	{

#if PULSEAUDIO
		if(scan_sv[i])
                        context.snr = 10 * log10(signals[i].snr) * 200;
#endif

		if(scan_sv[i])
		{
			printf("%2d, %4.4f, %10.4f, %7.4f,\n",
                                i + 1,
				10 * log10(signals[i].snr),
                                signals[i].doppler,
                                signals[i].phase);
		}
	}
#endif

} /* while(1) */

#if PULSEAUDIO
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
#endif

	fftwf_destroy_plan(ifft);
        for (int i = 0; i < MAX_SV; i++)
           if (scan_sv[i])
	      fftw_free(ca_fft[i]);

	fftw_free(prod);

	fftwf_destroy_plan(training_plan);
	fftw_free(training);
	fftw_cleanup();
	exit(EXIT_SUCCESS);
}
