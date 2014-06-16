#! /bin/bash
if [ ! -f igzip-src.tar.gz ] ; then
	if [ ! -f igzip_042.zip ] ; then
		wget https://software.intel.com/sites/default/files/managed/2d/63/igzip_042.zip
	fi

	mkdir -p igzip-src
	cd igzip-src
	unzip ../igzip_042.zip
	cd ..

	tar czf igzip-src.tar.gz igzip-src
	rm -fR igzip-src
fi

NASMVERSION=1.2.0

if [ ! -f yasm-${NASMVERSION}.tar.gz ] ; then
	wget http://www.tortall.net/projects/yasm/releases/yasm-${NASMVERSION}.tar.gz
fi

BASEDIR=${PWD}

tar xzf yasm-${NASMVERSION}.tar.gz
mv yasm-${NASMVERSION} yasm-${NASMVERSION}-src
mkdir -p yasm-${NASMVERSION}-build
cd yasm-${NASMVERSION}-build
../yasm-${NASMVERSION}-src/configure --prefix=${BASEDIR}/yasm-bin/${NASMVERSION}
make
make install
cd ..
rm -fR yasm-${NASMVERSION}-src yasm-${NASMVERSION}-build

tar xzf igzip-src.tar.gz
cd igzip-src/igzip
sed < Makefile "s|^YASM *:= *.*|YASM := ${BASEDIR}/yasm-bin/${NASMVERSION}/bin/yasm|" >Makefile.patched
mv Makefile.patched Makefile
sed < options.inc "s|^;%define GENOME_SAM|%define GENOME_SAM|;s|^;%define GENOME_BAM|%define GENOME_BAM|;s|;%define ONLY_DEFLATE|%define ONLY_DEFLATE|" > options.inc.patched
mv options.inc.patched options.inc
make
cd ../..

rm -fR ${BASEDIR}/yasm-bin

mkdir -p igzip-bin
mkdir -p igzip-bin/lib
mkdir -p igzip-bin/include
for i in `find igzip-src -regextype posix-extended -regex ".*\.(a|so)"` ; do
	cp -p $i igzip-bin/lib/
done
for i in igzip-src/include/types.h igzip-src/igzip/c_code/igzip_lib.h igzip-src/igzip/c_code/internal_state_size.h ; do
	cp -p $i igzip-bin/include/
done

cd igzip-bin/include
sed <igzip_lib.h "s|types.h|igzip_lib_types.h|" >igzip_lib.h.patched
mv igzip_lib.h.patched igzip_lib.h
mv types.h igzip_lib_types.h
cd ../..

tar czf igzip-bin.tar.gz igzip-bin

rm -fR igzip-src
rm -fR igzip-bin

rm -fR yasm-${NASMVERSION}.tar.gz
rm -fR igzip-src.tar.gz