
A simple microservice for creating image thumbnails.


# Dependencies

- C++11 compiler
- GraphicsMagick++ or ImageMagick++
- ZeroMQ
- Protocol Buffers
- POSIX (for syslog only)


# Client requirements

- Programming language with ZeroMQ and Protocol Buffers support
- ZeroMQ
- Protocol Buffers


# Interfacing how-to

1. Compile the Protocol Buffers in thumq.proto for your language of choice.

2. Connect to the service using a REQ or DEALER ZeroMQ socket (the address is
   specified as a command-line argument when starting the service).

3. Send a two-part message: a Request (see thumq.proto) and an image.

4. Receive a message: two parts on success or single part on error.  The first
   part is a Response (see thumq.proto) and the second is a
   scaled/cropped/stripped JPEG image.

