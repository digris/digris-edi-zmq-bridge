# Copyright (C) 2020 Matthias P. Braendli, http://opendigitalradio.org

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty || echo 'unknown'`\""
else
GITVERSION_FLAGS =
endif

bin_PROGRAMS=odr-edi2edi

INCLUDE=-Isrc

common_test_CFLAGS   =-Wall $(INCLUDE) $(GITVERSION_FLAGS)
common_test_CXXFLAGS =-Wall -std=c++11 $(INCLUDE) $(GITVERSION_FLAGS)
common_test_LDADD    = -lpthread
common_test_SOURCES  = src/main.cpp \
					   lib/crc.h lib/crc.c \
					   lib/ClockTAI.h lib/ClockTAI.cpp \
					   lib/Globals.cpp \
					   lib/Log.h lib/Log.cpp \
					   lib/ReedSolomon.h lib/ReedSolomon.cpp \
					   lib/RemoteControl.h \
					   lib/RemoteControl.cpp \
					   lib/Socket.h lib/Socket.cpp \
					   lib/ThreadsafeQueue.h \
					   lib/charset/charset.h lib/charset/charset.cpp \
					   lib/charset/utf8.h \
					   lib/charset/utf8/checked.h \
					   lib/charset/utf8/core.h \
					   lib/charset/utf8/unchecked.h \
					   lib/edi/PFT.hpp lib/edi/PFT.cpp \
					   lib/edi/STIDecoder.hpp lib/edi/STIDecoder.cpp \
					   lib/edi/STIWriter.hpp lib/edi/STIWriter.cpp \
					   lib/edi/ETIDecoder.hpp lib/edi/ETIDecoder.cpp \
					   lib/edi/eti.hpp lib/edi/eti.cpp \
					   lib/edi/buffer_unpack.hpp \
					   lib/edi/common.hpp lib/edi/common.cpp \
					   lib/edioutput/AFPacket.cpp \
					   lib/edioutput/AFPacket.h \
					   lib/edioutput/EDIConfig.h \
					   lib/edioutput/Interleaver.cpp \
					   lib/edioutput/Interleaver.h \
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

