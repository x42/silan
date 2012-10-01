## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	-g -Wall -O2 \
	-I$(top_srcdir) \
	-I$(top_srcdir)/audio_decoder/ \
	$(SILAN_CFLAGS)

bin_PROGRAMS = silan

silan_SOURCES = \
	main.c \
  $(top_srcdir)/audio_decoder/ad.h

silan_LDADD = \
	$(top_srcdir)/audio_decoder/libad.a \
	$(SILAN_LIBS) \
	$(SNDFILE_LIBS) \
	$(FFMPEG_LIBS)
