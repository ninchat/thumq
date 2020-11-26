GO		:= go
PROTOC		:= protoc
PYTHON		:= python3

build:
	$(GO) generate
	$(GO) build -o thumq ./cmd/thumq

check: build
	rm -rf test-output
	mkdir -p test-output/no-crop test-output/top-square
	PROTOC=$(PROTOC) $(PYTHON) test.py 128
	PROTOC=$(PROTOC) $(PYTHON) test.py --top-square 128

clean:
	rm -f thumq
	rm -rf test-output

.PHONY: build check clean
