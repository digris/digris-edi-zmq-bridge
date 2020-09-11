Overview
========

ODR-EDI2EDI is part of the ODR-mmbTools tool set. More information about the
ODR-mmbTools is available in the *guide*, available on the
[Opendigitalradio mmbTools page](http://www.opendigitalradio.org/mmbtools).

About ODR-EDI2EDI
=================

Sometimes you want to carry a DAB Ensemble using EDI over the Internet to a device that doesn't support EDI/TCP.
Carrying EDI/UDP over the Internet will not work because of burst packet loss.

With ODR-EDI2EDI, you can convert EDI/TCP to EDI/UDP on a small PC that is close to your device. It also allows you
buffer the EDI and release it at a controlled point in time depending on the in-band timestamp.

Every six seconds, a line with buffering time statistics is printed. If you use a process supervisor that writes this
output to a logfile, you may use the *doc/stats_edi2edi_munin.py* script to analyse the logfile and present the
statistics as [munin](http://munin-monitoring.org/) graphs.

You can also fan-out an EDI data stream to several destinations.

This tool can be considered to be the successor of ODR-ZMQ2EDI which is distributed as a part of
[ODR-DabMux](https://github.com/Opendigitalradio/ODR-DabMux).

Installation
============

Requirements: A C++11 compiler, autotools (debian packets `build-essential automake libtool`)

    ./bootstrap.sh
    ./configure
    make
    sudo make install

Licence
=======

See the files `LICENCE` and `COPYING`


Contributions and Contact
=========================

Contributions to this tool are welcome, you can reach users and developers through the
[ODR-mmbTools group](https://groups.io/g/odr-mmbtools)
or any other channels mentioned on the ODR website.
