AC_PREREQ(2.59)
AC_INIT([silan],[0.1.1],[robin@gareus.org])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_COPYRIGHT([Copyright (C) Robin Gareus 2012])

AM_INIT_AUTOMAKE

AC_PROG_INSTALL
AC_PROG_CC
AC_C_CONST
AC_C_INLINE
AC_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_PROG_LIBTOOL
AM_PROG_LIBTOOL
AM_PROG_CC_C_O
AC_SYS_LARGEFILE

LT_INIT

# Check for debugging flag
debug="no"
AC_ARG_ENABLE(debug,
	[AS_HELP_STRING(--enable-debug, [Enable debugging (false)])],
	[debug="$enableval"])
if test "$debug" = "yes"; then
	CFLAGS="-O0 -g -DDEBUG"
else
	CFLAGS="$CFLAGS -DNDEBUG"
fi

# default 'auto' configutaion
AC_ARG_ENABLE(ffmpeg,   AC_HELP_STRING([--disable-ffmpeg], [disable use of FFMPEG]), enable_ffmpeg=$enableval, enable_ffmpeg="auto")

SILAN_LIBS="-lm"

case $target_os in
	*darwin*)
		CFLAGS="$CFLAGS -fnested-functions"
		SILAN_LIBS="$SILAN_LIBS -Wl,-flat_namespace,-undefined,dynamic_lookup"
		;;
	*linux*)
		;;
	*)	
		;;
esac
AC_SUBST(SILAN_LIBS)
AC_SUBST(SILAN_CFLAGS)

# Check for strict flag
strict="no"
AC_ARG_ENABLE(strict,
	[AS_HELP_STRING(--enable-strict, [Enable strict compiler warnings and errors (false)])],
	[strict="$enableval"])
if test "$strict" = "yes"; then
	CFLAGS="$CFLAGS -ansi -Wall -Wextra -Wno-unused-parameter -Wconversion -Winit-self"
	CXXFLAGS="$CXXFLAGS -ansi -Wall -Wextra -Wno-unused-parameter -Wconversion -Winit-self -Woverloaded-virtual -Wsign-promo"
fi

dnl -----------------------------------------------------------------------------

AC_MSG_CHECKING([if -Wl,--as-needed works])
LDFLAGS_save=$LDFLAGS
LDFLAGS="$LDFLAGS -Wl,--as-needed"
AC_TRY_LINK([], [],
            [
             AC_MSG_RESULT([yes])
             LDFLAGS="$LDFLAGS -Wl,--as-needed"
            ], 
            [AC_MSG_RESULT([no])])
LDFLAGS=$LDFLAGS_save


dnl TODO: sndfile is optional if FFMPEG is avail
PKG_CHECK_MODULES(SNDFILE, sndfile)
AC_SUBST(SNDFILE_CFLAGS)
AC_SUBST(SNDFILE_LIBS)

dnl -----------------------------------------------------------------------------

AS_IF([test "$enable_ffmpeg" != "no"], [
	PKG_CHECK_MODULES(FFMPEG, [libavformat >= 51.0.0 libavcodec >= 51.0.0 libavutil >= 50.0.0], enable_ffmpeg=yes, enable_ffmpeg=no)
	])

if test "$enable_ffmpeg" = "yes"; then
	AC_DEFINE(HAVE_FFMPEG,1,[Use ffmpeg audio decoder])
fi
AM_CONDITIONAL(ENABLE_FFMPEG, test "$enable_ffmpeg" = "yes")
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

# Bolt on a few specific flags to CXXFLAGS that should always be used
CXXFLAGS="$CXXFLAGS -pipe -fmessage-length=139 -fdiagnostics-show-location=every-line"
CFLAGS="$CFLAGS -pipe  -fmessage-length=139 -fdiagnostics-show-location=every-line"

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([audio_decoder/Makefile])
AC_OUTPUT

AC_MSG_RESULT([])
AC_MSG_RESULT([Audio decoders:])
AC_MSG_RESULT([  libsndfile:                 yes])
AC_MSG_RESULT([  ffmpeg decoder:             $enable_ffmpeg])
AC_MSG_RESULT([])
