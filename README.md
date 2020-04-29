ODR-EDI2EDI
===========

Sometimes you want to carry EDI over the Internet to a device that doesn't
support EDI/TCP. Carrying EDI/UDP over the Internet will not work because of
burst packet loss, and EDI resend is not implemented.

With ODR-EDI2EDI, you can convert EDI/TCP to EDI/UDP on a small PC that is
close to your device. It also allows you buffer the EDI and release it at a
controlled point in time depending on the in-band timestamp.

You can also fan-out an EDI data stream to several destinations.

This tool can be considered to be the successor of ODR-ZMQ2EDI which is
distributed as a part of ODR-DabMux.

Installation
------------

Requirements: A C++11 compiler, autotools (debian: `build-essential automake libtool`)

    ./bootstrap.sh
    ./configure
    make
    sudo make install
