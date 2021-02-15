# Copyright (C) 2020 Matthias P. Braendli, http://opendigitalradio.org

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty || echo 'unknown'`\""
else
GITVERSION_FLAGS =
endif

bin_PROGRAMS=odr-edi2edi

INCLUDE=-Ilib

odr_edi2edi_CFLAGS   =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
odr_edi2edi_CXXFLAGS =-Wall -std=c++11 $(INCLUDE) $(GITVERSION_FLAGS)
odr_edi2edi_LDADD    = -lpthread
odr_edi2edi_SOURCES  = src/main.cpp \
					   src/EDISender.h src/EDISender.cpp \
					   lib/crc.h lib/crc.c \
					   lib/Globals.cpp \
					   lib/Log.h lib/Log.cpp \
					   lib/ReedSolomon.h lib/ReedSolomon.cpp \
					   lib/RemoteControl.h \
					   lib/RemoteControl.cpp \
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

