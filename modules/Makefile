#  Build loadable kernel module outside of kernel tree

TARGET	:= bbrdb
#WARN	:= -W -Wall -Wstrict-prototypes -Wmissing-prototypes
WARN	:= 
INCLUDE := -isystem /lib/modules/`uname -r`/build/include
CFLAGS  := -O2 -DMODULE -DMODVERSIONS -D__KERNEL__ $(WARN) $(INCLUDE)
MODMAP  := /lib/modules/`uname -r`/modules.usbmap

$(TARGET).o:	$(TARGET).c

.PHONY:	clean

default:	$(TARGET).o

install:	default
	cp -f $(TARGET).o /lib/modules/`uname -r`/kernel/drivers/usb
	@if grep bbrdb $(MODMAP) > /dev/null; then \
		echo bbrdb spec already in modules.usbmap;	\
	else \
		echo Adding bbrdb spec to $(MODMAP);	\
		cat modules.usbmap.append >> $(MODMAP); \
	fi
	/sbin/depmod -a
	@if [ -c /dev/usb/bbrdb0 ]; then \
		echo /dev entries already exist; \
	else \
		echo Creating /dev entries; \
		mknod /dev/usb/bbrdb0 c 180 160; \
		mknod /dev/usb/bbrdb1 c 180 161; \
		mknod /dev/usb/bbrdb2 c 180 162; \
		mknod /dev/usb/bbrdb3 c 180 163; \
		mknod /dev/usb/bbrdb4 c 180 164; \
		mknod /dev/usb/bbrdb5 c 180 165; \
		mknod /dev/usb/bbrdb6 c 180 166; \
		mknod /dev/usb/bbrdb7 c 180 167; \
		chmod 666 /dev/usb/bbrdb?; \
	fi
	-/sbin/rmmod bbrdb
	/sbin/insmod bbrdb

install_devkit:
	test "" != "$(DEVKIT)"
	mkdir -p $(DEVKIT)/modules
	cp $(TARGET).c $(DEVKIT)/modules
	cp Makefile $(DEVKIT)/modules
	cp modules.usbmap.append $(DEVKIT)/modules

clean clobber:
	-rm -f $(TARGET).o
