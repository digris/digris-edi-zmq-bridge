# Copyright (C) 2024 Matthias P. Braendli, http://opendigitalradio.org

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty || echo 'unknown'`\""
else
GITVERSION_FLAGS =
endif

bin_PROGRAMS=digris-edi2edi digris-edimcast2edi digris-zmq-converter
noinst_LIBRARIES = libedi2edi.a

INCLUDE=-Ilib -Isrc

digris_edi2edi_CFLAGS   = -Wall $(INCLUDE) $(ZMQ_CPPFLAGS) $(GITVERSION_FLAGS)
digris_edi2edi_CXXFLAGS = -Wall $(INCLUDE) $(ZMQ_CPPFLAGS) $(GITVERSION_FLAGS)
digris_edi2edi_LDADD    = -lpthread libedi2edi.a $(LIBATOMIC_LIBS) $(ZMQ_LIBS)
digris_edi2edi_SOURCES  = src/edi2edi.h src/edi2edi.cpp

digris_edimcast2edi_CFLAGS   = -Wall $(INCLUDE) $(GITVERSION_FLAGS)
digris_edimcast2edi_CXXFLAGS = -Wall $(INCLUDE) $(GITVERSION_FLAGS)
digris_edimcast2edi_LDADD    = -lpthread libedi2edi.a $(LIBATOMIC_LIBS)
digris_edimcast2edi_SOURCES  = src/edimcast2edi.cpp \
							src/mpe_deframer.hpp src/mpe_deframer.cpp \
							src/gse_deframer.hpp src/gse_deframer.cpp \
							src/EDIReceiver.hpp src/EDIReceiver.cpp

digris_zmq_converter_CFLAGS   = -Wall $(INCLUDE) $(ZMQ_CPPFLAGS) $(GITVERSION_FLAGS)
digris_zmq_converter_CXXFLAGS = -Wall $(INCLUDE) $(ZMQ_CPPFLAGS) $(GITVERSION_FLAGS)
digris_zmq_converter_LDADD    = libedi2edi.a $(ZMQ_LIBS)
digris_zmq_converter_SOURCES  = src/zmq/main.cpp \
					   src/zmq/Sender.h \
					   src/zmq/Sender.cpp

libedi2edi_a_CFLAGS   = -Wall $(INCLUDE) $(GITVERSION_FLAGS)
libedi2edi_a_CXXFLAGS = -Wall $(INCLUDE) $(GITVERSION_FLAGS)
libedi2edi_a_SOURCES  = src/receiver.h src/receiver.cpp \
					   src/EDISender.h src/EDISender.cpp \
					   src/zmq/edi2zmq.hpp src/zmq/edi2zmq.cpp \
					   src/zmq/metadata.h src/zmq/metadata.cpp \
					   src/zmq/dabOutputZMQ.h src/zmq/dabOutputZMQ.cpp \
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
			  lib/fec/README.md src/fec/LICENSE

