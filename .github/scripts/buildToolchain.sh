#!/bin/bash

ROOT_DIR="$(pwd)"
BINUTILS_VERSION="2.46.0"
GCC_VERSION="16.1.0"
NUM_JOBS="4"

if [ $# -eq 2 ]; then
	PACKAGE_NAME="$1"
	TARGET_NAME="$2"
	BUILD_OPTIONS=""
elif [ $# -eq 3 ]; then
	PACKAGE_NAME="$1"
	TARGET_NAME="$2"
	BUILD_OPTIONS="--build=x86_64-linux-gnu --host=$3"
else
	echo "Usage: $0 <package name> <target triplet> [host triplet]"
	exit 0
fi

# GNU's mirror redirector (ftpmirror.gnu.org) occasionally hands out a dead
# mirror and returns a transient 502/503/504. Retry a few times with backoff,
# and fall back to ftp.gnu.org (the canonical, always-up origin) if the
# mirror redirector keeps failing.
download_with_retry() {
	local path="$1"
	local out="$2"
	local urls=(
		"https://ftpmirror.gnu.org/$path"
		"https://ftp.gnu.org/$path"
	)
	local attempt
	for url in "${urls[@]}"; do
		for attempt in 1 2 3 4 5; do
			echo "Downloading $url (attempt $attempt)..."
			if wget --tries=1 --timeout=30 --retry-on-http-error=502,503,504 -O "$out" "$url"; then
				return 0
			fi
			rm -f "$out"
			sleep $((attempt * 5))
		done
	done
	echo "ERROR: failed to download $path from all mirrors." >&2
	return 1
}

## Download binutils and GCC

if [ ! -d binutils-$BINUTILS_VERSION ]; then
	download_with_retry "gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz" "binutils-$BINUTILS_VERSION.tar.xz" \
		|| exit 1
	tar Jxf binutils-$BINUTILS_VERSION.tar.xz \
		|| exit 1

	rm -f binutils-$BINUTILS_VERSION.tar.xz
fi

if [ ! -d gcc-$GCC_VERSION ]; then
	download_with_retry "gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz" "gcc-$GCC_VERSION.tar.xz" \
		|| exit 1
	tar Jxf gcc-$GCC_VERSION.tar.xz \
		|| exit 1

	cd gcc-$GCC_VERSION

	# download_prerequisites (fetches GMP/MPFR/MPC) hits the same GNU mirror
	# network, so it's worth a few retries too before giving up entirely.
	for attempt in 1 2 3; do
		echo "Fetching GCC prerequisites (attempt $attempt)..."
		contrib/download_prerequisites && break
		if [ "$attempt" -eq 3 ]; then
			echo "ERROR: contrib/download_prerequisites failed after 3 attempts." >&2
			exit 1
		fi
		sleep $((attempt * 10))
	done

	cd ..
	rm -f gcc-$GCC_VERSION.tar.xz
fi



## Build binutils

mkdir -p binutils-build
cd binutils-build

../binutils-$BINUTILS_VERSION/configure \
	--prefix="$ROOT_DIR/$PACKAGE_NAME" \
	$BUILD_OPTIONS \
	--target=$TARGET_NAME \
	--with-float=soft \
	--disable-docs \
	--disable-nls \
	--disable-werror \
	|| exit 2
make -j $NUM_JOBS \
	|| exit 2
make install-strip \
	|| exit 2

cd ..
rm -rf binutils-build

## Build GCC

mkdir -p gcc-build
cd gcc-build

../gcc-$GCC_VERSION/configure \
	--prefix="$ROOT_DIR/$PACKAGE_NAME" \
	$BUILD_OPTIONS \
	--target=$TARGET_NAME \
	--with-float=soft \
	--disable-docs \
	--disable-nls \
	--disable-werror \
	--disable-libada \
	--disable-libssp \
	--disable-libquadmath \
	--disable-threads \
	--disable-libgomp \
	--disable-libstdcxx-pch \
	--disable-hosted-libstdcxx \
	--enable-languages=c,c++ \
	--without-isl \
	--without-headers \
	--with-gnu-as \
	--with-gnu-ld \
	|| exit 3
make -j $NUM_JOBS \
	|| exit 3
make install-strip \
	|| exit 3

cd ..
rm -rf gcc-build

## Package toolchain

#cd $PACKAGE_NAME

#zip -9 -r ../$PACKAGE_NAME-$GCC_VERSION.zip . \
#	|| exit 4

#cd ..
#rm -rf $PACKAGE_NAME
