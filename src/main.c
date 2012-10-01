#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ad.h"

int debug_level = 1;
#define PERIODSIZE (1024)

struct silan_settings {
	char *fn;
	float threshold;
	enum {PM_NONE = 0, PM_SAMPLES, PM_AUDACITY} printmode;
	float hpf_tc;
	float holdoff_sec;
};

struct silan_state {
	float *hpf_x; // HPF buffer (per channel)
	float *hpf_y; // HPF buffer (per channel)

	double rms_sum;
	double *window;
	double *window_cur;
	double *window_end;
	int     window_size;

	int state; // 0: silent, 1:non-silent
	int holdoff;
};


void print_time(struct silan_settings *ss, struct adinfo *nfo, struct silan_state *st, int64_t frameno) {
	switch (ss->printmode) {
		case PM_SAMPLES:
			printf("%9"PRIi64" %s\n", frameno, st->state?"On":"Off");
			break;
		case PM_AUDACITY:
			printf("%7lf\t%lf\t%s\n", (double)frameno/nfo->sample_rate, (double)(frameno + 1)/nfo->sample_rate, st->state?"On":"Off");
			break;
		default:
			break;
	}
}

void process_audio(
		struct silan_settings *ss,
		struct adinfo *nfo,
		struct silan_state *st,
		unsigned int n_frames,
		int64_t frame_cnt,
		float *buf
		) {

	int i,c;
	const unsigned int n_channels = nfo->channels;
	const double t2 = (ss->threshold * ss->threshold) * st->window_size;
	const float a = ss->hpf_tc;

	for (i=0; i < n_frames; ++i){
		int above_threshold = 0;
		for (c=0; c < n_channels; ++c) {
			// high pass filter
			const float x0 = buf[i * n_channels + c];
			const float x1 = st->hpf_x[c];
			const float y1 = st->hpf_y[c];
			float y0 = a * (y1 + x0 - x1);
			st->hpf_x[c] = x0;
			st->hpf_y[c] = y0;

			st->rms_sum -= *st->window_cur;
			*st->window_cur = y0 * y0;
			st->rms_sum += *st->window_cur;

			st->window_cur++;
			if (st->window_cur >= st->window_end)
				st->window_cur = st->window;

			if (st->rms_sum > t2)
				above_threshold |=1;
		}

#if 1
		if (above_threshold) {
			st->state|=2;
		} else {
			st->state&=~2;
		}

		if (((st->state&1)==1) ^ ((st->state&2)==2)) {
			if (++ st->holdoff > (ss->holdoff_sec * nfo->sample_rate)) {
				if ((st->state&2)) st->state|=1; else st->state&=~1;
				print_time(ss, nfo, st, frame_cnt + i - st->holdoff);
			}
		} else {
			st->holdoff = 0;
		}
#else
		const double rms = st->rms_sum;
		if ((st->state&1) && rms < t2) {
			if (!(st->state&2)
			//if (++ st->holdoff > (ss->holdoff_sec * nfo->sample_rate))
			st->state&=~1;
			print_time(ss, nfo, st, frame_cnt + i);
		} else
		if (!(st->state&1) && rms > t2) {
			st->state|=1;
			print_time(ss, nfo, st, frame_cnt + i);
		}
#endif
	}
}

int doit(struct silan_settings *s) {
	struct adinfo nfo;
	struct silan_state state;
	int64_t frame_cnt = 0;
	float * abuf = NULL;
	ad_clear_nfo(&nfo);

	void *sf = ad_open(s->fn, &nfo);
	if (!sf) {
		fprintf(stderr, "can not open file '%s'\n", s->fn);
		return 1;
	}

	dump_nfo(1, &nfo);
	abuf = (float*) malloc(PERIODSIZE * nfo.channels * sizeof(float));

	state.holdoff = 0;
	state.state = 0; // silent
	state.hpf_x = (float*) calloc(nfo.channels, sizeof(float));
	state.hpf_y = (float*) calloc(nfo.channels, sizeof(float));

	state.window_size = nfo.channels * nfo.sample_rate / 50;
	state.window = (double*) calloc(state.window_size, sizeof(double));
	state.window_cur = state.window;
	state.window_end = state.window + (state.window_size);
	state.rms_sum = 0;

	// TODO check if alloc's succeeded.

	while (1) {
		int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
		if (rv <= 1) break;
		process_audio(s, &nfo, &state, PERIODSIZE, frame_cnt, abuf);
		frame_cnt += rv / nfo.channels;
	}

	// TODO close off current state.. -- silence at end..

#if 1 // DEBUG
	if (frame_cnt != nfo.frames) {
		fprintf(stderr, "DEBUG; framecount mismatch: %lld/%lld\n", frame_cnt, nfo.frames);
	}
#endif

	free(abuf);
	free(state.hpf_x);
	free(state.hpf_y);
	free(state.window);

	ad_close(sf);
	ad_free_nfo(&nfo);
	return 0;
}

int main(int argc, char **argv) {
	struct silan_settings settings;

	settings.threshold = 0.0005; //  10^(db/20.0) with db < 0.
	settings.printmode = PM_SAMPLES;
	settings.hpf_tc = .98; // 0..1  == RC / (RC + dt)  // f = 1 / (2 M_PI RC)
	settings.holdoff_sec = 0.3;

	// TODO getopt, help, version..
	if (argc > 1) {
		settings.fn = strdup(argv[1]);
	} else {
		settings.fn = strdup ("/tmp/test.wav");
	}

	ad_init();
	int rv = doit(&settings);

	/* clean up*/
	if (settings.fn) free(settings.fn);

	return rv;
}
