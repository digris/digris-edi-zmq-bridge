# DIGRIS EDI and ZMQ Bridge Usage Scenarios

## EDI/tcp to EDI/udp

### Simple EDI/tcp to EDI/udp

    ./digris-edi-tcp-converter -c edi1.digris.net:4001 -w -1000 -d 226.33.9.1 -p 5000 -f 3 -r /var/tmp/edi2edi.socket -C "chronyc waitsync 2 0.01"

  * `-C` Waiting chrony sync before start
  * `-c` Connect to EDI/tcp source `edi1.digris.net:8861`
  * `-d` Send EDI/udp to `226.33.9.1:5000` with FEC 3 `-f`
  * `-w` Delivering 1000ms before (`-1000`) the timestamp expire.

### 2 EDI/tcp to 1 EDI/udp

    ./digris-edi-tcp-converter -c edi1.digris.net:4001 -c edi2.digris.net:4001 -m merge -w -1000 -d 226.33.9.1 -p 5000 -f 3 -r /var/tmp/edi2edi.socket -C "chronyc waitsync 2 0.01"

  * ```-m merge``` merge frame from 2 different source. This two source need to come from the same odr-dabmux.
  * ```-m switch``` switch between 2 different source can coming from main or slave odr-dabmux

## EDI/tcp to ZMQ

    ./digris-edi-tcp-converter -z *:9050 -f 3 -c edi1.digris.net:4001 -w -1000 -r /var/tmp/edi2edi.socket -C "chronyc waitsync 2 0.01"

Connect to EDI/tcp edi1.digris.net:4001, and add ZMQ listener on port 9050


## EDI/udp to EDI/tcp

    ./digris-edi-udp-converter -m 232.20.10.1 -p 12000 -T 8050

Listen EDI multicast on 232.20.10.1:12000 and add EDI/tcp listener on port 8050


### EDI/udp with MPE to EDI/tcp

    ./digris-edi-udp-converter -m 226.29.2.1 -p 5000 -l 8001 -F 301:239.0.1.11:5001

Receive from satellite receiver the UDP multicast on 226.29.2.1:5000 and filtering on PID 301, IP 239.0.1.11, and port 5001

The satellite receiver can be a dvbstream process: `dvbstream -c 0 8192 -i 226.29.2.1 -r 5000`


### EDI/udp with GSE to EDI/tcp

    ./digris-edi-udp-converter -m 226.29.2.4 -p 5000 -l 8020 -G 1

Receive from satellite receiver the UDP multicast on 226.29.2.4:5000 and filtering DVB-GSE group 1 (MIS)


## EDI/tcp to EDI/tcp - proxy

    ./digris-edi-tcp-converter -c edi1.digris.net:4001 -T 8050

You can connect many time to 8050, only one stream coming from edi1.digris.net

