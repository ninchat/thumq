GO		:= go
PROTOC		:= protoc
PYTHON		:= python3

build:
	$(GO) generate
	$(GO) build -o thumq ./cmd/thumq

check: build
	rm -rf test-output
	PROTOC=$(PROTOC) $(PYTHON) test.py
	PROTOC=$(PROTOC) $(PYTHON) test.py --top-square
	PROTOC=$(PROTOC) $(PYTHON) test.py --convert

clean:
	rm -f thumq
	rm -rf test-output

.PHONY: build check clean
