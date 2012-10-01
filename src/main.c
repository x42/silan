#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ad.h"

int debug_level = 1;

struct silan_settings {
	char *fn;
	float threshold;
};

struct silan_state {
	float *signal;
	double rms;
	float lpf_tc;
	int state;
	double sr;
};


#define PERIODSIZE (1024)

void process_audio(
		struct silan_settings *ss,
		struct silan_state *st,
		unsigned int n_frames,
		unsigned int n_channels,
		int64_t frame_cnt,
		float *buf
		) {

	int i,c;
	const float t2 = ss->threshold * ss->threshold;
	const float a = st->lpf_tc;
	float rms = 0;
	for (i=0; i < n_frames; ++i){
		for (c=0; c < n_channels; ++c) {
			const float x0 = buf[i * n_channels + c];
			const float y1 = st->signal[c];
			float y0 = y1 + a * (x0 - y1);
			st->signal[c] = y0;
			rms += y0 * y0;
		}
	}
	rms/= (n_frames * n_channels);

	if (st->state && rms < t2) {
		printf("%7lf\t%lf\tOff\n", frame_cnt/st->sr, (frame_cnt + i)/st->sr);
		st->state = 0;
	} else
	if (!st->state && rms > t2) {
		printf("%7lf\t%lf\tOn\n", frame_cnt/st->sr, (frame_cnt + i)/st->sr);
		st->state = 1;
	}
	st->rms = sqrt(rms);
	//printf(" %8lld  %.3f   %.3f %.3f %.3f %.3f\r", frame_cnt, st->rms);
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

	state.sr = nfo.sample_rate;
	state.lpf_tc = 0.3; // 14000.0 / nfo.sample_rate; // TODO assert(0 < a < 1)
	state.rms = 0;
	state.state = 0; // silent
	state.signal = calloc(nfo.channels, sizeof(float));
	abuf = malloc(nfo.channels * PERIODSIZE * sizeof(float));

	// TODO check if alloc's succeeded.

	while (1) {
		int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
		if (rv <= 1) break;
		process_audio(s, &state, PERIODSIZE, nfo.channels, frame_cnt, abuf);
		frame_cnt += rv / nfo.channels;
	}

#if 1 // DEBUG
	if (frame_cnt != nfo.frames) {
		fprintf(stderr, "DEBUG; framecount mismatch: %lld/%lld\n", frame_cnt, nfo.frames);
	}
#endif

	free(abuf);
	free(state.signal);

	ad_close(sf);
	ad_free_nfo(&nfo);
	return 0;
}

int main(int argc, char **argv) {
	struct silan_settings settings;

	settings.threshold = 0.0005; //  10^(db/20) ;; db < 0.

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
