#!/bin/bash

#
# script to install the BB devkit
#
DEST=/opt/ique/sdk-v1.5
DESTLINK=/opt/ique/sdk

#
# check whether executed as root
#
if id | grep "^uid=0(root)" > /dev/null; then
	echo starting root installation of iQue SDK
	echo installing compilation tools
        apt-get -y install binutils-mips-linux-gnu
        apt-get -y install gcc-10-mips-linux-gnu
        apt-get -y install libelf1
        apt-get -y install libelf-dev
	mkdir -p /usr/mips-linux/bin
        cp as /usr/mips-linux/bin
        #cp as /usr/bin/mips-linux-as
	#
	# Copy to devkit to the real install location, unless we're there already
	#
	if [ `/bin/pwd` != "$DEST" ]; then
		echo copying SDK contents to $DEST ...
		rm -rf $DEST
		mkdir -p $DEST
		tar cf - . | (cd $DEST; tar xf -)
		cd $DEST
		find . \( -type f -or -type d \) -exec chmod u+w "{}" ";"
		rm -f $DESTLINK
		ln -s $DEST $DESTLINK
	else
		echo skip copy to $DEST, not needed
	fi
	make CC=mips-linux-gnu-gcc-10 CFLAGS="-I /usr/src/linux-headers-5.4.0-77-generic/include/config -I /usr/src/linux-headers-5.13.0-19/include/linux/" -C modules install
	echo root installation complete, each developer must also run this script individually
	exit 0
fi

#
# install user files
#
echo starting individual user installation of iQue SDK
if [ -d $DEST/per_user ]; then
	cd $DEST
else
	echo Directory $DEST does not exist:  run install as root first.
	exit 1
fi

if [ "x" == "x$ROOT" ]; then
	echo ROOT environment variable is not set.
	exit 1
fi

if [ ! -d $ROOT ]; then
	echo Directory $ROOT does not exist:  please create it before running this script.
	exit 1
fi

(cd ./per_user; tar cf - .) | (cd $ROOT; tar xf -)
mkdir -p $ROOT/doc
ln -sf $DEST/index.html $ROOT/doc
