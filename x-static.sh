#!/bin/bash
## compile a statically linked version of silan for linux
## this requires a static build of ffmpeg; see x-pbuildstatic.sh
##

#path to the static ffmpeg installation
: ${PFX=$HOME/local}
#path to output directory -- /silan*.tgz will end up there
: ${RESULT=/tmp}

VERSION=$(git describe --tags HEAD || echo "X.X.X")
TRIPLET=$(gcc -print-multiarch)
OUTFN=silan-$TRIPLET-$VERSION

LIBF=$PFX/lib
BINF=$PFX/bin
export PKG_CONFIG_PATH=${LIBF}/pkgconfig

# ffmpeg needs this libs
LIBDEPS=" \
 libsndfile.a \
 libFLAC.a \
 libmp3lame.a \
 libspeex.a \
 libtheoraenc.a \
 libtheoradec.a \
 libogg.a \
 libvorbis.a \
 libvorbisenc.a \
 libvorbisfile.a \
 libschroedinger-1.0.a \
 liborc-0.4.a \
 libgsm.a \
 libbluray.a \
 libxvidcore.a \
 libopus.a \
 libbz2.a \
 libvpx.a \
 libopenjpeg.a \
 libx264.a \
 libz.a \
 "

if test "`hostname`" == "soyuz"; then
	LIBDEPS="$LIBDEPS \
 libX11.a \
 libxcb.a \
 libXau.a \
 libXdmcp.a \
 "
fi

# resolve paths to static libs on the system
SLIBS=""
for SLIB in $LIBDEPS; do
	echo "searching $SLIB.."
	SL=`find /usr/lib -name "$SLIB"`
	if test -z "$SL"; then
		echo "not found."
		exit 1
	fi
	SLIBS="$SLIBS $SL"
done

LIBAD_SRC=" \
  audio_decoder/ad_soundfile.c \
  audio_decoder/ad_plugin.c \
  audio_decoder/ad_ffmpeg.c \
 "

# compile silan
test -f config.h || ./autogen.sh
mkdir -p tmp
gcc -DNDEBUG \
  -Wall -O3 \
  -o tmp/$OUTFN -Iaudio_decoder -I. src/*.c ${LIBAD_SRC} \
	`pkg-config --cflags libavcodec libavformat libavutil libswscale` \
	${CFLAGS} \
	${LIBF}/libavformat.a \
	${LIBF}/libavcodec.a \
	${LIBF}/libswscale.a \
	${LIBF}/libavdevice.a \
	${LIBF}/libavutil.a \
	\
	$SLIBS \
	-lm -ldl -pthread \
|| exit 1

strip tmp/$OUTFN
ls -lh tmp/$OUTFN
ldd tmp/$OUTFN

# give any arg to disable bundle
test -n "$1" && exit 1

# build .tgz bundle
rm -rf $RESULT/$OUTFN $RESULT/$OUTFN.tgz
mkdir $RESULT/$OUTFN
cp tmp/$OUTFN $RESULT/$OUTFN/silan
cp README $RESULT/$OUTFN/README
cp silan.1 $RESULT/$OUTFN/silan.1
cd $RESULT/ ; tar czf $RESULT/$OUTFN.tgz $OUTFN ; cd -
rm -rf $RESULT/$OUTFN
ls -lh $RESULT/$OUTFN.tgz
