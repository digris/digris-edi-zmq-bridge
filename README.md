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

Statistics are made available through a UNIX DGRAM Socket, which also serves as remote control interface.

You can also fan-out an EDI data stream to several destinations.

This tool can be considered to be the successor of ODR-ZMQ2EDI which is distributed as a part of
[ODR-DabMux](https://github.com/Opendigitalradio/ODR-DabMux).

Remote Control
==============

ODR-EDI2EDI contains a remote-control function that allows changing settings at runtime.
Please see `./edi2edi_remote.py` for an example on how to use it.

Example:

    odr-edi2edi -r /tmp/edi2edi.socket <OTHER OPTIONS>
    ./edi2edi_remote.py -s /tmp/edi2edi.socket --stats

Statistics
----------

The following stats are available through the remote control interface. Unless mentioned otherwise, they
are all 64-bit counters.

 * `num_poll_timeout`: Number of times the receive poll timed out for all inputs. Timeout interval: 24ms

Inputs:

 * `num_connects`: Number of times the input reconnected.
 * `num_late`: Number of frames that arrived late (taking into account -w value).
 * `margin_to_delivery`: (not a counter) Margin in milliseconds before frame is too late to be delivered.
 * `margin`: (not a counter) Margin in milliseconds before frame is too late to be modulated.
 * `most_recent_connect_error`: A string giving the error message of the most recent connection error.
 * `most_recent_connect_error_timestamp`: timestamp (UNIX epoch) of the most recent connection error.

Outputs:

 * `num_frames`: Number of frames sent. Monitor the rate of this statistic to assess if the output is free of interruptions.
 * `num_dlfc_discontinuities`: Number of Frame Counter value errors. Every occurrence of the discontinuity enables backoff.
 * `num_queue_overruns`: Number of times the output queue overruns. This should always stay at zero.
 * `num_dropped`: Number of frames that were dropped either because they were late, or because of backoff. In merge
   mode, this includes frames that were transmitted from one input but arrived late on another input. Therefore, an
   increasing `num_dropped` value does not mean that output frames were actually missing.
 * `late_score`: (not a counter) Score between 0 and 100 indicating how often frames are late.


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
or any other channels mentioned on the [ODR](https://www.opendigitalradio.org) website.
