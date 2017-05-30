CC = gcc# -V 2.95.2
LD_DYNAMIC = gcc -shared#	for SunOS 5, Linux
#LD_DYNAMIC = ld -Bdynamic#	for SunOS 4

RM = rm -f
MV = mv
MKDIR = mkdir -p
LN = ln
AR = ar
RANLIB = ranlib
INSTALL = install -c
CO = co
CI = ci
WC = wc
TEE = tee
ETAGS = etags
RLOG = rlog -h
ZIP = zip -0
OBJDUMP = objdump

CFLAGS = -pipe -O2 -fPIC

TARGET = tsctest
OBJ = tsctest.o x86tsc.o

all: ${TARGET}

${TARGET}: ${OBJ}
	gcc -o $@ ${OBJ}

clean:
	${RM} *~ ${TARGET} ${OBJ}
