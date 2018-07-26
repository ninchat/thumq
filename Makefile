PKG_CONFIG	:= pkg-config
PROTOC		:= protoc
PYTHON		:= python3

PKGS		:= libseccomp protobuf GraphicsMagick++

CPPFLAGS	:= -pthread -DNDEBUG $(shell $(PKG_CONFIG) $(PKGS) --cflags-only-I)
CXXFLAGS	:= -std=c++11 -g -Wall -O2 $(shell $(PKG_CONFIG) $(PKGS) --cflags-only-other)
LDFLAGS		:= -pthread $(shell $(PKG_CONFIG) $(PKGS) --libs-only-L)
LIBS		:= -lzmq -lmagic $(shell $(PKG_CONFIG) $(PKGS) --libs-only-l)

sources		:= service.cpp thumq.pb.cc

thumq: $(sources) io.hpp thumq.pb.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $(sources) $(LIBS)

thumq.pb.cc thumq.pb.h: thumq.proto
	$(PROTOC) --cpp_out=. thumq.proto

check: thumq
	rm -rf test-output
	mkdir -p test-output/no-crop test-output/top-square
	PROTOC=$(PROTOC) $(PYTHON) test.py 128
	PROTOC=$(PROTOC) $(PYTHON) test.py --top-square 128

clean:
	rm -f thumq thumq.pb.cc thumq.pb.h
	rm -rf test-output

.PHONY: check clean
