/* silan - silence analyzer
 *
 * Copyright (C) 2012-2013 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE // basename in string.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>

#include "ad.h"
#include "config.h"

int debug_level = 0;
#define PERIODSIZE (1024)

enum {
	B_EN = 1,  ///< enable first/last mode
	B_FAST = 2,///< enable 'fast' mode (process backwards after finding sound-on)
	B_F1 = 4,  ///< found first sound-on
	B_REV = 8, ///< reading/procssing backwards
	B_F2 = 16, ///< found last sound-off
};

struct silan_settings {
	char *fn;
	char *command;
	float threshold;
	enum {PM_SAMPLES, PM_SECONDS} printmode;
	enum {PF_TXT = 0, PF_CSV, PF_JSON, PF_AUDACITY, PF_COMMAND} printformat;
	float hpf_tc;
	float holdoff_sec;
	int progress;
	char *outfilename;
	FILE *outfile;
	int first_last_only;
	int include_initial;
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
	int64_t holdoff; // holdoff frame counter
	int64_t prev_on; // frame-number of latest 'On' state - used for delayed print -- audacity only
	int64_t prev_off; // frame-number of latest 'Off' state - used for delayed print
	int first_last; // print only first & last
	int cnt;
	int64_t initial_silence_countdown;
};

void run_commmand (
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		struct silan_state * const st,
		const int64_t frameno)
{
	char tmp[64];
  char **argv; int argc = 0;
	argv = (char**) calloc(5, sizeof(char*));

	argv[argc++] = strdup(basename(ss->command));
	if (st->state&1) {
		argv[argc++] = strdup ("sound");
	} else {
		argv[argc++] = strdup ("silence");
	}
	sprintf(tmp, "%lf", ((double)frameno/nfo->sample_rate) );
	argv[argc++] = strdup (tmp);

	argv[argc] = 0; // sentinel

	pid_t pid = fork();

	if (pid==0) {
		/* child process */
		execve (ss->command, (char *const *) argv, environ);
		fprintf(stderr,"ERR: exec returned.\n");
		exit(127);
	}

	/* parent/main process */
	if (pid < 0 ) {
		fprintf(stderr,"ERR: can not fork child process\n");
	}

	/* clean up */
	for (argc = 0; argv[argc]; ++argc) {
		free(argv[argc]);
	}
	free (argv);
}

void print_time(
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		const int indent,
		const int64_t frameno) {
	switch (ss->printmode) {
		case PM_SAMPLES:
			if (indent)
				fprintf(ss->outfile, "%9"PRIi64, frameno);
			else
				fprintf(ss->outfile, "%" PRIi64, frameno);
			break;
		case PM_SECONDS:
		default:
			if (indent)
				fprintf(ss->outfile, "%7lf", ((double)frameno/nfo->sample_rate) );
			else
				fprintf(ss->outfile, "%lf", ((double)frameno/nfo->sample_rate) );
	}
}

void format_time(
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		struct silan_state * const st,
		const int64_t frameno) {
	switch (ss->printformat) {
		case PF_TXT:
			print_time(ss, nfo, 1, frameno);
			fprintf(ss->outfile, " Sound %s\n", (st->state&1)?"On":"Off");
			break;
		case PF_JSON:
			if (st->state&1) {
				st->prev_on = frameno;
			} else if (st->prev_on>=0) {
				if (st->cnt++)
					fprintf(ss->outfile, ",");
				fprintf(ss->outfile, " [ ");
				print_time(ss, nfo, 0, st->prev_on);
				fprintf(ss->outfile, ", ");
				print_time(ss, nfo, 0, frameno);
				fprintf(ss->outfile, " ]");
				st->prev_on = -1;
			}
			break;
		case PF_AUDACITY:
			if (st->state&1) {
				st->prev_on = frameno;
			} else if (st->prev_on>=0) {
				fprintf(ss->outfile, "%7lf\t%lf\tSound\n", (double)st->prev_on/nfo->sample_rate, (double)(frameno)/nfo->sample_rate);
				st->prev_on = -1;
			}
			break;
		case PF_COMMAND:
			// TODO, mark time, add a grace period
			run_commmand (ss, nfo, st, frameno);
		default:
			break;
	}
	fflush(ss->outfile);
}

void process_audio(
		struct silan_settings const * const ss,
		struct adinfo const * const nfo,
		struct silan_state * const st,
		const unsigned int n_frames,
		const int64_t frame_cnt,
		float const * const buf
		) {

	unsigned int i,c;
	const unsigned int n_channels = nfo->channels;
	const double t2 = (ss->threshold * ss->threshold) * st->window_size;
	const float a = ss->hpf_tc;
	const int64_t holdoff_threshold = (ss->holdoff_sec * nfo->sample_rate);

	i = st->first_last & B_REV ? n_frames - 1 : 0;
	/* process audio sample by sample */
	while (1) {
		int above_threshold = 0;
		for (c=0; c < n_channels; ++c) {
			/* high pass filter */
			const float x0 = buf[i * n_channels + c];
			const float x1 = st->hpf_x[c];
			const float y1 = st->hpf_y[c];
			const float y0 = a * (y1 + x0 - x1);
			st->hpf_x[c] = x0;
			st->hpf_y[c] = y0;

			/* calculate RMS */
			st->rms_sum -= *st->window_cur;
			*st->window_cur = y0 * y0;
			st->rms_sum += *st->window_cur;

			st->window_cur++;
			if (st->window_cur >= st->window_end)
				st->window_cur = st->window;

			if (st->rms_sum > t2)
				above_threshold |=1;
		}

		/* hold state */
		if (above_threshold) {
			st->state|=2;
		} else {
			st->state&=~2;
		}

		if (((st->state&1)==1) ^ ((st->state&2)==2)) {
			if (++st->holdoff >= holdoff_threshold) {
				st->initial_silence_countdown = -1; // disable
				if ((st->state&2)) {
					st->state|=1;
					st->prev_off = -1;
				} else {
					st->state&=~1;
					st->prev_off = frame_cnt + i + 1 - st->holdoff;
				}
				if (!(st->first_last & B_F1)) {
					format_time(ss, nfo, st, frame_cnt + i + 1 - st->holdoff);
				}
				if (st->first_last & B_REV) {
					/* we're reading backwards
					 * -> sound-start -> beginning of silence (when reading fwd)
					 */
					st->state ^= 1;
					format_time(ss, nfo, st, frame_cnt + i + 1 + st->holdoff);
					st->state ^= 1;
					st->first_last |= B_F2;
					break;
				}
				if ((st->first_last & B_EN) && (st->state&1) ) {
					st->first_last |= B_F1;
					if (st->first_last & B_FAST) {
						break;
					}
				}
			}
		} else {
			st->holdoff = 0;

			if (st->initial_silence_countdown > 0 && (st->state&1)==0) {
				if (--st->initial_silence_countdown == 0) {
					format_time(ss, nfo, st, 0);
				}
			}
		}

		/* loop: increment or decrement */
		if (st->first_last & B_REV) {
			if (i == 0 ) break;
			else --i;
		} else {
			++i;
			if (i == n_frames) break;
		}
	} /* end for each sample */
}

static void reset_state(struct silan_state * const st, int nch) {
		st->holdoff = 0;
		st->state = 0;
		st->rms_sum = 0;
		st->prev_on = -1;
		st->prev_off = -1;
		memset(st->hpf_x, 0, nch * sizeof(float));
		memset(st->hpf_y, 0, nch * sizeof(float));
		memset(st->window, 0, st->window_size * sizeof(double));
		st->window_cur = st->window;
		st->window_end = st->window + (st->window_size);
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
		if (debug_level>=0)
			fprintf(stderr, "! cannot open audio file '%s'\n", s->fn);
		return 1;
	}

	ad_dump_nfo(1, &nfo);
	abuf = (float*) malloc(PERIODSIZE * nfo.channels * sizeof(float));

	state.holdoff = 0;
	state.cnt = 0;
	state.state = 0; // start silent
	state.initial_silence_countdown = s->include_initial ? (s->holdoff_sec * nfo.sample_rate) : 0;
	state.hpf_x = (float*) calloc(nfo.channels, sizeof(float));
	state.hpf_y = (float*) calloc(nfo.channels, sizeof(float));

	state.window_size = nfo.channels * nfo.sample_rate / 50;
	state.window = (double*) calloc(state.window_size, sizeof(double));
	state.window_cur = state.window;
	state.window_end = state.window + (state.window_size);
	state.rms_sum = 0;
	state.prev_on = -1;
	state.prev_off = -1;
	state.first_last = s->first_last_only & (B_EN|B_FAST);


	if (!abuf || ! state.hpf_x || ! state.hpf_y || !state.window) {
		if (debug_level>=0)
			fprintf(stderr, "! out-of-memory\n");
		rv=1;
		goto bailout;
	}

	/* output prefixes - if any */
	switch (s->printformat) {
		case PF_JSON:
			fprintf(s->outfile, "{ \"sound\":[");
		default:
			break;
	}

	/* process audio file data */
	while (1) {
		int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
		if (rv < 1) break;

		process_audio(s, &nfo, &state, rv / nfo.channels, frame_cnt, abuf);

		if ((state.first_last & (B_EN|B_FAST|B_F1)) == (B_EN|B_FAST|B_F1)) {
			/* first boundary found -- continue decoding backwards from end */
			break;
		}

		frame_cnt += rv / nfo.channels;

		if (s->progress) {
			fprintf(stderr, " %3.1f%%     \r", frame_cnt * 100.0 / nfo.frames); fflush(stderr);
		}
	}

	if ((state.first_last & (B_EN|B_FAST|B_F1)) == (B_EN|B_FAST|B_F1)) {
		/* reset state  - prepare for backwards reading */
		state.first_last |= B_REV;
		reset_state(&state, nfo.channels);

		int64_t pos = nfo.frames - PERIODSIZE;
		/* read audio file backwards from last frame */
		while (1) {
			if (pos > 0 && pos < frame_cnt) {

			}
			if (pos < 0 || pos < frame_cnt || ad_seek(sf, pos) < 0) {
				break;
			}

			int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
			if (rv < 1) {
				break;
			}

			process_audio(s, &nfo, &state, rv / nfo.channels, pos, abuf);
			pos -= rv / nfo.channels;

			if ((state.first_last & B_F2)) {
				rv = 0;
				break;
			}
		if (s->progress) {
			fprintf(stderr, " ???.?f%%     \r"); fflush(stderr);
		}
		}
	}

	if (((state.first_last & B_F2) && ((state.state&1) == 0))
			|| (state.first_last & (B_F2|B_REV)) == B_REV ) {
		/* reverse decoding failed */
		reset_state(&state, nfo.channels);
		state.first_last &= ~B_EN;
		state.state = 1;

		/* resume forward decoding from where we left off */
		if (ad_seek(sf, frame_cnt) < 0) {
			rv = 1;
			goto bailout;
		}

		while (1) {
			int rv = ad_read(sf, abuf, PERIODSIZE * nfo.channels);
			if (rv < 1) break;

			process_audio(s, &nfo, &state, rv / nfo.channels, frame_cnt, abuf);

			if ((state.first_last & (B_EN|B_FAST|B_F1)) == (B_EN|B_FAST|B_F1)) {
				/* first boundary found -- continue decoding backwards from end */
				break;
			}

			frame_cnt += rv / nfo.channels;

			if (s->progress) {
				fprintf(stderr, " %3.1f%%     \r", frame_cnt * 100.0 / nfo.frames); fflush(stderr);
			}
		}
	}

	if ((state.state & 1) && !(state.first_last & B_REV)) {
		/* close off combined on/off labels */
		state.state = 0;
		format_time(s, &nfo, &state, nfo.frames);
	} else if ((state.first_last & (B_F1|B_F2)) == B_F1) {
		/* close off first/last only */
		state.state = 0;
		format_time(s, &nfo, &state, state.prev_off >=0 ? state.prev_off : nfo.frames);
	}

	/* output postfixes - if any */
	switch (s->printformat) {
		case PF_JSON:
			fprintf(s->outfile, "], \"file duration\":");
			print_time(s, &nfo, 0, nfo.frames);
			fprintf(s->outfile, ", \"sample rate\":%d", nfo.sample_rate);
			fprintf(s->outfile, "}\n");
		default:
			break;
	}

	if (s->progress) {
		fprintf(stderr,"        \n");
	}

	if (debug_level > 1 &&  frame_cnt != nfo.frames) {
		fprintf(stderr, "Note: frame-count mismatch: %"PRIi64"/%"PRIi64"\n", frame_cnt, nfo.frames);
	}

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
	{"bounds", no_argument, 0, 'b'},
	{"fastbounds", no_argument, 0, 'B'},
	{"command", required_argument, 0, 'C'},
	{"format", required_argument, 0, 'f'},
	{"filter", required_argument, 0, 'F'},
	{"help", no_argument, 0, 'h'},
	{"initial", no_argument, 0, 'i'},
	{"output", required_argument, 0, 'o'},
	{"progress", no_argument, 0, 'p'},
	{"quiet", no_argument, 0, 'q'},
	{"threshold", required_argument, 0, 's'},
	{"holdoff", required_argument, 0, 't'},
	{"unit", required_argument, 0, 'u'},
	{"verbose", no_argument, 0, 'v'},
	{"version", no_argument, 0, 'V'},
	{NULL, 0, NULL, 0}
};

static void usage (int status) {
  printf ("silan - Audiofile Silence Analyzer.\n\n");
  printf ("Usage: silan [ OPTIONS ] <file-name>\n\n");
  printf ("Options:\n\
  -h, --help                 display this help and exit\n\
  -b, --bounds               skip silence mid file.\n\
	                           print start/end boundaries only.\n\
  -B, --fastbounds           same as -b, except the sound-off is detected by\n\
	                           decoding backwards from the end.\n\
	                           This is much faster but also inacurate.\n\
  -f, --format <format>      specify output format (default: 'txt')\n\
  -F, --filter <float>       high-pass filter coefficient (default:0.98)\n\
                             disable: 1.0; range 0 < val <= 1.0\n\
  -i, --include-initial      display initial state (don't assume initial\n\
                             silence at start).\n\
  -o, --output <filename>    write data to file instead of stdout\n\
  -p, --progress             show progress info on stderr\n\
  -q, --quiet                inhibit error messages\n\
  -s, --threshold <float>    RMS signal threshold (default 0.001 ^= -60dB)\n\
                             postfix with 'd' to specify decibels\n\
  -t, --holdoff <float>      holdoff time in seconds (default 0.5)\n\
  -u, --unit <unit>          specify output unit (default: 'seconds')\n\
  -v, --verbose              increase debug-level (can be used multiple times)\n\
  -V, --version              print version information and exit\n\
\n");
  printf ("\n\
This application reads a single audio file and analyzes it for\n\
silent periods. Timestamps/ranges of silence are printed to standard output.\n\
\n\
Valid output formats are: txt, JSON, audacity (label file)\n\
\n\
Valid output units are: samples, seconds (audacity format uses seconds regardless).\n\
\n\
Sound is detected if the signal level exceeds a given threshold for a\n\
duration of at least <holdoff> time.\n\
Note that the returned timestamps are corrected for the holdoff-time.\n\
\n\
The fast boundary scan can decrease the time it takes to analyze a file\n\
at the cost of accuracy.\n\
Use --fastbounds with care. Due to low-pass filtering and RMS calculation\n\
the results will be different (+- .1 sec), furthermore some codecs are not\n\
suitable for backwards decoding or sample-accurate seeking and may skew the\n\
timestamp by one second or more.\n\
The fast boundary scan mode requires a seekable file and does not work with\n\
streams.\n\
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
			   "b" 	/* boundaries */
			   "B" 	/* boundaries */
			   "C:" /* command */
			   "f:"	/* output format */
			   "F:"	/* high-pass filter cutoff */
			   "i"  /* include-initial */
			   "o:" /* outfile */
			   "p" 	/* progress */
			   "s:"	/* signal threhold */
			   "t:"	/* holdoff time */
			   "u:"	/* unit */
			   "q" 	/* quiet */
			   "v" 	/* verbose */
			   "V",	/* version */
			   long_options, (int *) 0)) != EOF) {
		switch (c)
		{
			case 'b':
				ss->first_last_only |= B_EN;
				break;

			case 'B':
				ss->first_last_only |= B_EN | B_FAST;
				break;

			case 'C':
				free(ss->command);
				ss->command = strdup (optarg);
				ss->printformat = PF_COMMAND;
				break;

			case 'f':
				if      (!strncasecmp(optarg, "txt" , strlen(optarg))) ss->printformat = PF_TXT;
				else if (!strncasecmp(optarg, "text" , strlen(optarg))) ss->printformat = PF_TXT;
				else if (!strncasecmp(optarg, "json", strlen(optarg))) ss->printformat = PF_JSON;
				else if (!strncasecmp(optarg, "audacity", strlen(optarg))) ss->printformat = PF_AUDACITY;
				else {
					fprintf(stderr, "! invalid output format specified\n");
					usage(EXIT_FAILURE);
				}
				break;

			case 'i':
				ss->include_initial = 1;
				break;

			case 'u':
				if      (!strncasecmp(optarg, "samples" , strlen(optarg))) ss->printmode = PM_SAMPLES;
				else if (!strncasecmp(optarg, "seconds" , strlen(optarg))) ss->printmode = PM_SECONDS;
				else {
					fprintf(stderr, "! invalid output unit specified\n");
					usage(EXIT_FAILURE);
				}
				break;

			case 'F':
				ss->hpf_tc= atof(optarg);
				if (ss->hpf_tc<=0 || ss->hpf_tc > 1.0) {
					fprintf(stderr, "! invalid high-pass filter time constant. need: 0 < value <= 1.0\n");
					usage(EXIT_FAILURE);
				}
				break;

			case 'o':
				free(ss->outfilename);
				ss->outfilename = strdup(optarg);
				break;

			case 'p':
				ss->progress = 1;
				break;

			case 's':
				{
					float v;
					if (strlen(optarg)> 0 && optarg[strlen(optarg)-1] == 'd') {
						v = pow(10.0, fabsf(atof(optarg))/-20.0);
					} else {
						v = atof(optarg);
					}
					if (v>=0 && v<=1) {
						fprintf(stderr, "Info: signal threshold: %f ^= %.3fdBFS\n",  v, 20.0 * log10f(v)); // XXX
						ss->threshold = v;
					} else {
						fprintf(stderr, "! invalid signal threshold.\n");
						usage(EXIT_FAILURE);
					}
				}
				break;

			case 't':
				ss->holdoff_sec = atof(optarg);
				if (ss->holdoff_sec < 0) ss->holdoff_sec = 0;
				break;

			case 'q':
				debug_level=-1;
				break;

			case 'v':
				if (debug_level>=0)
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
	int rv = 0;
	struct silan_settings settings;

	/* default values */
	settings.printmode = PM_SECONDS;
	settings.printformat = PF_TXT;
	settings.threshold = 0.001; //  10^(db/20.0) with db < 0.
	settings.hpf_tc = .98; // 0..1  == RC / (RC + dt)  // f = 1 / (2 M_PI RC)
	settings.holdoff_sec = 0.5;
	settings.fn = NULL;
	settings.command = NULL;
	settings.outfile = NULL;
	settings.outfilename = NULL;
	settings.progress = 0;
	settings.first_last_only = 0;
	settings.include_initial = 0;

	/* parse options */
	int i = decode_switches (&settings, argc, argv);

	ad_set_debuglevel(debug_level);

	if (argc > i) {
		settings.fn = strdup(argv[i]);
	} else {
		usage(EXIT_FAILURE);
	}

	/* sanity check */
	if (settings.command) {
		if (settings.printformat != PF_COMMAND) {
			fprintf(stderr, "! -f and -C (format and command) options cannot be combined.\n");
			goto cleanup;
		}
		if (settings.outfilename) {
			fprintf(stderr, "! -C and -o (command and output file) are exclusive options.\n");
			goto cleanup;
		}
	}

	/* open output file - if any */
	if (settings.outfilename) {
		settings.outfile = fopen(settings.outfilename, "w");
		if (!settings.outfile) {
			if (debug_level >= 0)
				fprintf(stderr, "! cannot open output file '%s'.\n", settings.outfilename);
			rv=1;
			goto cleanup;
		}
	} else {
		settings.outfile = stdout;
	}

	/* initialize audio decoders */
	ad_init();

	/* all systems go */
	rv = doit(&settings);

cleanup:
	/* clean up*/
	free(settings.fn);
	free(settings.command);
	if (settings.outfilename && settings.outfile) {
		free(settings.outfilename);
		fclose(settings.outfile);
	}

	return rv;
}
