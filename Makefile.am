## Process this file with automake to produce Makefile.in

SUBDIRS = audio_decoder src

man_MANS = silan.1

EXTRA_DIST = silan.1

man: src/silan
	help2man -N -l -n "Soundfile Silence Analyzer" -o silan.1 ./src/silan
