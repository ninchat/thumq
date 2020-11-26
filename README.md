
A simple microservice for creating image thumbnails.


# Dependencies

- Go
- Unix sockets


# Client requirements

- Unix sockets
- Protocol Buffers support


# Interfacing how-to

1. Compile the Protocol Buffers in thumq.proto for your language of choice.

2. Connect to the unix socket.

3. Send a two-part message: a Request (see thumq.proto) and an image.  Each
   message part is prefixed with a 32-bit little-endian size field.

4. Receive a two-part message.  The first part is a Response (see thumq.proto)
   and the second is a scaled/cropped/stripped JPEG image (or empty on error).

