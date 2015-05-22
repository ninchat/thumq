PKG_CONFIG	:= pkg-config
PROTOC		:= protoc
PYTHON		:= python

MAGICKPP_PKG	:= GraphicsMagick++

CPPFLAGS	+= -pthread -DNDEBUG $(shell $(PKG_CONFIG) $(MAGICKPP_PKG) --cflags-only-I)
CXXFLAGS	+= -g -Wall -O2 $(shell $(PKG_CONFIG) $(MAGICKPP_PKG) --cflags-only-other)
LDFLAGS		+= -pthread $(shell $(PKG_CONFIG) $(MAGICKPP_PKG) --libs-only-L)
LIBS		+= -lzmq -lprotobuf $(shell $(PKG_CONFIG) $(MAGICKPP_PKG) --libs-only-l)

sources		:= service.cpp thumq.pb.cc

thumq: $(sources) io.hpp thumq.pb.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $(sources) $(LIBS)

thumq.pb.cc thumq.pb.h: thumq.proto
	$(PROTOC) --cpp_out=. thumq.proto

check: thumq
	PROTOC=$(PROTOC) $(PYTHON) test.py 128
	PROTOC=$(PROTOC) $(PYTHON) test.py --top-square 128

clean:
	rm -f thumq thumq.pb.cc thumq.pb.h

.PHONY: check clean
