INCLUDE_PATH = -I.

FLAGS = -Wall -O2 -march=native -ffast-math

LIB_PATH = -L.

CC = clang $(FLAGS)

LIB = $(LIB_PATH) -lm

OBJS = gen_ringtone.o

ALL = gen_ringtone

all : $(ALL)

gen_ringtone : $(OBJS)
	$(CC) $(INCLUDE_PATH) -o gen_ringtone $(OBJS) $(LIB)

%.o : %.c
	$(CC) $(INCLUDE_PATH) -c

clean :
	- /bin/rm -f *.o $(ALL)
