#ifndef __AD_H__
#define __AD_H__
#include <unistd.h>
#include <stdint.h>

#ifndef __PRI64_PREFIX
#if (defined __X86_64__ || defined __LP64__)
# define __PRI64_PREFIX  "l"
#else
# define __PRI64_PREFIX  "ll"
#endif
#endif

#ifndef PRIu64
# define PRIu64   __PRI64_PREFIX "u"
#endif
#ifndef PRIi64
# define PRIi64   __PRI64_PREFIX "i"
#endif

struct adinfo {
	unsigned int sample_rate;
	unsigned int channels;
	int64_t length; //milliseconds
	int64_t frames; //total number of frames (eg a frame for 16bit stereo is 4 bytes).
	int     bit_rate;
	int     bit_depth;
	char *  meta_data;
};

/* global init function - register codecs */
void ad_init();

/* low level API */
void *  ad_open  (const char *, struct adinfo *);
int     ad_close (void *);
int64_t ad_seek  (void *, int64_t);
ssize_t ad_read  (void *, float*, size_t);
int     ad_info  (void *sf, struct adinfo *nfo);

void    ad_clear_nfo     (struct adinfo *nfo);
void    ad_free_nfo      (struct adinfo *nfo);

int ad_finfo             (const char *, struct adinfo *);
ssize_t ad_read_mono_dbl (void *, struct adinfo *, double*, size_t);
void dump_nfo            (int dbglvl, struct adinfo *nfo);
#endif
