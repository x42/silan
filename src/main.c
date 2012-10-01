#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <math.h>

#include "ad.h"
#include "config.h"

int debug_level = 0;
#define PERIODSIZE (1024)

struct silan_settings {
	char *fn;
	float threshold;
	enum {PM_NONE = 0, PM_SAMPLES, PM_SECONDS, PM_AUDACITY} printmode;
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
	int64_t prev_on;
};


void print_time(
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		struct silan_state * const st,
		const int64_t frameno) {
	switch (ss->printmode) {
		case PM_SAMPLES:
			printf("%9"PRIi64" %s\n", frameno, (st->state&1)?"On":"Off");
			break;
		case PM_SECONDS:
			printf("%7lf %s\n", (double)frameno/nfo->sample_rate, (st->state&1)?"On":"Off");
			break;
		case PM_AUDACITY:
			if (st->state&1) {
				st->prev_on = frameno;
			} else if (st->prev_on>=0) {
				printf("%7lf\t%lf\tOn\n", (double)st->prev_on/nfo->sample_rate, (double)(frameno)/nfo->sample_rate);
				st->prev_on = -1;
			}
			break;
		default:
			break;
	}
}

void process_audio(
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		struct silan_state * const st,
		const unsigned int n_frames,
		const int64_t frame_cnt,
		float const * const buf
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
			if (++st->holdoff >= (ss->holdoff_sec * nfo->sample_rate)) {
				if ((st->state&2)) {
					st->state|=1;
				} else {
					st->state&=~1;
				}
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

int doit(struct silan_settings const * const s) {
	int rv = 0;
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
	state.prev_on = -1;


	if (!abuf || ! state.hpf_x || ! state.hpf_y || !state.window) {
		fprintf(stderr, "out-of-memory\n");
		rv=1;
		goto bailout;
	}

	while (1) {
		int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
		if (rv <= 1) break;
		process_audio(s, &nfo, &state, PERIODSIZE, frame_cnt, abuf);
		frame_cnt += rv / nfo.channels;
	}

	// close off PM_AUDACITY labels
	if (state.prev_on >= 0) {
		state.state = 0;
		print_time(s, &nfo, &state, frame_cnt);
	}

#if 1 // DEBUG
	if (frame_cnt != nfo.frames) {
		fprintf(stderr, "DEBUG; framecount mismatch: %lld/%lld\n", frame_cnt, nfo.frames);
	}
#endif

bailout:
	free(abuf);
	free(state.hpf_x);
	free(state.hpf_y);
	free(state.window);

	ad_close(sf);
	ad_free_nfo(&nfo);
	return rv;
}


/**************************
 * main application code
 */

static struct option const long_options[] =
{
  {"format", required_argument, 0, 'f'},
  {"filter", required_argument, 0, 'F'},
  {"help", no_argument, 0, 'h'},
  {"threshold", required_argument, 0, 's'},
  {"holdoff", required_argument, 0, 't'},
  {"verbose", no_argument, 0, 'v'},
  {"version", no_argument, 0, 'V'},
  {NULL, 0, NULL, 0}
};

static void usage (int status) {
  printf ("silan - Audiofile Silence Analyzer.\n\n");
  printf ("Usage: silan [ OPTIONS ] <file-name>\n\n");
  printf ("Options:\n\
  -h, --help                 display this help and exit\n\
  -f <format>                specify output format (default: 'samples')\n\
  -s <float>                 RMS signal threshold (default 0.0005 ~= -66dB)\n\
                             postfix with 'd' to specify decibles\n\
  -t <float>                 holdoff time in seconds (default 0.3)\n\
  -v, --verbose              increase debug-level (can be used multiple times)\n\
  -V, --version              print version information and exit\n\
\n");
  printf ("\n\
This application reads a single audio file and analyzes it for\n\
silent periods.\n\
\n\
Valid output formats are: samples, seconds, audacity (label file)\n\
\n");
  printf ("Report bugs to Robin Gareus <robin@gareus.org>\n"
          "Website and manual: <https://github.com/x42/silan>\n"
	  );
  exit (status);
}

static int decode_switches (struct silan_settings * const ss, int argc, char **argv) {
  int c;

  while ((c = getopt_long (argc, argv,
			   "h"	/* help */
			   "f:"	/* output format */
			   "F:"	/* high-pass filter cutoff */
			   "s:"	/* signal threhold */
			   "t:"	/* holdoff time */
			   "v" 	/* verbose */
			   "V",	/* version */
			   long_options, (int *) 0)) != EOF) {
		switch (c)
		{
			case 'f':
				if      (!strcasecmp(optarg, "samples")) ss->printmode = PM_SAMPLES;
				else if (!strcasecmp(optarg, "seconds")) ss->printmode = PM_SECONDS;
				else if (!strcasecmp(optarg, "audacity")) ss->printmode = PM_AUDACITY;
				else {
					fprintf(stderr,"! invalid output format specified\n");
					usage(EXIT_FAILURE);
				}
				break;
			case 'F':
				ss->hpf_tc= atof(optarg);
				if (ss->hpf_tc<=0 || ss->hpf_tc > 1.0) {
					fprintf(stderr,"! invalid high-pass filter time constant. need: 0 < value <= 1.0\n");
					usage(EXIT_FAILURE);
				}
				break;

			case 's':
				{
					float v;
					if (strlen(optarg)> 0 && optarg[strlen(optarg)-1] == 'd') {
						v = pow(10.0, fabsf(atof(optarg))/-20.0);
						printf("Signal threshold: %f\n",  v); // XXX
					} else {
						v = atof(optarg);
					}
					if (v>=0 && v<=1) {
						ss->threshold = v;
					} else {
						fprintf(stderr,"! invalid signal threshold.\n");
						usage(EXIT_FAILURE);
					}
				}
				break;

			case 't':
				ss->holdoff_sec = atof(optarg);
				if (ss->holdoff_sec < 0) ss->holdoff_sec = 0;
				break;

			case 'v':
				debug_level++;
				break;

			case 'V':
				printf ("silan version %s\n\n", VERSION);
				printf ("Copyright (C) GPL 2012 Robin Gareus <robin@gareus.org>\n");
				exit (0);

			case 'h':
				usage (0);

			default:
				usage (EXIT_FAILURE);
				break;
		}
	}
  return optind;
}


int main(int argc, char **argv) {
	struct silan_settings settings;

	/* default values */
	settings.printmode = PM_SAMPLES;
	settings.threshold = 0.0005; //  10^(db/20.0) with db < 0.
	settings.hpf_tc = .98; // 0..1  == RC / (RC + dt)  // f = 1 / (2 M_PI RC)
	settings.holdoff_sec = 0.3;
	settings.fn = NULL;

	/* parse options */
  int i = decode_switches (&settings, argc, argv);

	if (argc > i) {
		settings.fn = strdup(argv[i]);
	} else {
		usage(EXIT_FAILURE);
	}

	/* initialize audio decoders */
	ad_init();

	/* all systems go */
	int rv = doit(&settings);

	/* clean up*/
	if (settings.fn) free(settings.fn);

	return rv;
}
