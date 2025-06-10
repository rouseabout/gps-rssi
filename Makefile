OPTFLAGS = -O3 -flto
CFLAGS = -g -Wall $(OPTFLAGS)
LDFLAGS = $(OPTFLAGS)

all: read-s8 read-s16 read-iq2 rssi

read-s8: read-s8.o

read-s16: read-s16.o

read-iq2: read-iq2.o

rssi: LDLIBS = -lm -lfftw3f -lfftw3 -lportaudio
rssi: rssi.o prn.o

clean:
	rm -f *.o
	rm -f read-s8 read-s16 read-iq2 rssi
