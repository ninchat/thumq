
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

3. Send a message containing 32-bit little-endian size of the encoded Request
   (see thumq.proto), the encoded Request, and an image.

4. Receive a message containing 32-bit little-endian size of an encoded
   Response (see thumq.proto), the encoded Response, and a
   scaled/cropped/stripped JPEG image.

