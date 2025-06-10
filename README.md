### NAVSTAR Global Positioning System (GPS) Received Signal Strength Indicator (RSSI)

Program based on <https://github.com/psas/gps>

### Compiling

Debian/Ubuntu:

```
sudo apt-get install libfftw3-dev portaudio19-dev pv
make
```

### Offline test

Download <https://sourceforge.net/projects/gnss-sdr/files/data/2013_04_04_GNSS_SIGNAL_at_CTTC_SPAIN.tar.gz/download> and extract `2013_04_04_GNSS_SIGNAL_at_CTTC_SPAIN.dat`.

```
cat 2013_04_04_GNSS_SIGNAL_at_CTTC_SPAIN.dat | ./read-s16 | pv --quiet --rate-limit $(expr 8 \* 4000000) | ./rssi 4000000 0 32
```

Program output should match following.

```
32, 21.9214,  6459.9595, 79.7940,
32, 22.3261,  6459.1348, 79.7940,
32, 21.8351,  6457.7690, 79.7940,
```

### Run experiment (USRP)

To run an experiment using the Ettus Research USRP Hardware Driver (UHD):

1. Install USRP software (<https://files.ettus.com/manual/page_install.html#install_linux>).
2. Attach antenna to USRP RX2 port.
3. Attach USRP to computer.
4. Select GPS SV to track (in this case PRN 32 was chosen).
5. Run following script.

```
fs=2000000
g=72
sv=32
uhd_rx_cfile -A RX2 -r $fs -f 1575420000 -g $g -v /dev/stdout | ./rssi $fs 0 $sv
```

### Run experiment (RTL-SDR)

To run an experiment using the RTL-SDR.

1. Install RTL-SDR software (`sudo apt-get install rtl-sdr`).
2. Attach antenna to RTL-SDR.
3. Attach RTL-SDR to computer.
4. Select GPS SV to track (in this case PRN 32 was chosen).
5. Run following script.

```
fs=2000000
flags="-g 72"
sv=32
rtl_sdr -f 1575420000 -s $fs -p 75 $flags - | ./read-s8 | ./rssi $fs 0 $sv

```

## Contact

Peter Ross <pross@xvid.org>

GPG Fingerprint: A907 E02F A6E5 0CD2 34CD 20D2 6760 79C5 AC40 DD6B
