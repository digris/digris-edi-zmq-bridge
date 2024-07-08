# Copyright (C) 2024 Matthias P. Braendli, http://opendigitalradio.org

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty || echo 'unknown'`\""
else
GITVERSION_FLAGS =
endif

bin_PROGRAMS=odr-edi2edi odr-edimcast2edi odr-zmq2edi
noinst_LIBRARIES = libedi2edi.a

INCLUDE=-Ilib -Isrc

odr_edi2edi_CFLAGS   =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
odr_edi2edi_CXXFLAGS =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
odr_edi2edi_LDADD    = -lpthread libedi2edi.a $(LIBATOMIC_LIBS)
odr_edi2edi_SOURCES  = src/main.h src/main.cpp

odr_edimcast2edi_CFLAGS   =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
odr_edimcast2edi_CXXFLAGS =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
odr_edimcast2edi_LDADD    = -lpthread libedi2edi.a $(LIBATOMIC_LIBS)
odr_edimcast2edi_SOURCES  = src/edimcast2edi.cpp \
							src/mpe_deframer.hpp src/mpe_deframer.cpp \
							src/EDIReceiver.hpp src/EDIReceiver.cpp

odr_zmq2edi_CFLAGS   = -Wall $(INCLUDE) $(ZMQ_CPPFLAGS) $(PTHREAD_CFLAGS) $(GITVERSION_FLAGS)
odr_zmq2edi_CXXFLAGS = -Wall $(INCLUDE) $(PTHREAD_LIBS) $(ZMQ_CPPFLAGS) $(GITVERSION_FLAGS)
odr_zmq2edi_LDADD    = libedi2edi.a $(ZMQ_LIBS) $(PTHREAD_CFLAGS) $(PTHREAD_LIBS)
odr_zmq2edi_SOURCES  = src/zmq2edi/zmq2edi.cpp \
					   src/zmq2edi/Sender.h \
					   src/zmq2edi/Sender.cpp \
					   src/zmq2edi/metadata.h \
					   src/zmq2edi/metadata.cpp \
					   src/zmq2edi/dabOutputZMQ.h

libedi2edi_a_CFLAGS   =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
libedi2edi_a_CXXFLAGS =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
libedi2edi_a_SOURCES  = src/receiver.h src/receiver.cpp \
					   src/EDISender.h src/EDISender.cpp \
					   lib/crc.h lib/crc.c \
					   lib/Globals.cpp \
					   lib/Log.h lib/Log.cpp \
					   lib/ReedSolomon.h lib/ReedSolomon.cpp \
					   lib/Socket.h lib/Socket.cpp \
					   lib/ThreadsafeQueue.h \
					   lib/edi/PFT.hpp lib/edi/PFT.cpp \
					   lib/edi/buffer_unpack.hpp \
					   lib/edi/common.hpp lib/edi/common.cpp \
					   lib/edi/ETIDecoder.hpp lib/edi/ETIDecoder.cpp \
					   lib/edi/eti.hpp \
					   lib/edioutput/AFPacket.cpp \
					   lib/edioutput/AFPacket.h \
					   lib/edioutput/EDIConfig.h \
					   lib/edioutput/PFT.cpp \
					   lib/edioutput/PFT.h \
					   lib/edioutput/TagItems.cpp \
					   lib/edioutput/TagItems.h \
					   lib/edioutput/TagPacket.cpp \
					   lib/edioutput/TagPacket.h \
					   lib/edioutput/Transport.cpp \
					   lib/edioutput/Transport.h \
					   lib/fec/char.h \
					   lib/fec/decode_rs.h lib/fec/decode_rs_char.c \
					   lib/fec/encode_rs.h lib/fec/encode_rs_char.c \
					   lib/fec/fec.h \
					   lib/fec/init_rs.h lib/fec/init_rs_char.c \
					   lib/fec/rs-common.h

EXTRA_DIST	= COPYING NEWS README.md ChangeLog \
			  lib/fec/README.md src/fec/LICENSE \
			  src/charset/README

