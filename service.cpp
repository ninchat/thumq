#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <syslog.h>

#include <zmq.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <Magick++.h>

#include "thumq.pb.h"

namespace protobuf = google::protobuf;

using thumq::Request;
using thumq::Response;

namespace {

class IO
{
	IO(IO &);
	void operator=(const IO &);

public:
	/**
	 * @param socket  for use in a single receive-send cycle.
	 */
	explicit IO(zmq::socket_t &socket) throw ():
		handled(false),
		m_socket(socket),
		m_received(false)
	{
	}

	/**
	 * Does nothing if a complete message hasn't been received.  Sends a
	 * complete message (response_header and response_image) if handled, or an
	 * incomplete message if not.
	 */
	~IO() throw (zmq::error_t)
	{
		if (!m_received)
			return;

		send_part(response_header, handled);

		if (handled)
			send_part(response_image, false);
	}

	/**
	 * @return true if a complete message was received (request_header and
	 *         response_image), or false if an incomplete message was received.
	 */
	bool receive()
	{
		if (receive_part(request_header)) {
			if (receive_part(request_image)) {
				zmq::message_t extraneous;

				while (receive_part(extraneous)) {
				}
			}

			return true;
		}

		return false;
	}

	zmq::message_t request_header;
	zmq::message_t request_image;
	zmq::message_t response_header;
	zmq::message_t response_image;
	bool handled;

private:
	bool receive_part(zmq::message_t &message)
	{
		m_socket.recv(&message);

		int more;
		size_t length = sizeof more;
		m_socket.getsockopt(ZMQ_RCVMORE, &more, &length);

		if (!more)
			m_received = true;

		return more;
	}

	void send_part(zmq::message_t &message, bool more)
	{
		int flags = 0;

		if (more)
			flags |= ZMQ_SNDMORE;

		m_socket.send(message, flags);
	}

	zmq::socket_t &m_socket;
	bool m_received;
};

static bool decode_request(zmq::message_t &message, Request &request)
{
	protobuf::io::ArrayInputStream array(message.data(), message.size());
	protobuf::io::CodedInputStream coded(&array);

	return request.MergePartialFromCodedStream(&coded);
}

static void encode_response(const Response &response, zmq::message_t &message)
{
	message.rebuild(response.ByteSize());

	protobuf::io::ArrayOutputStream array(message.data(), message.size());
	protobuf::io::CodedOutputStream coded(&array);

	response.SerializeWithCachedSizes(&coded);
}

static void convert_image(Magick::Image &image, int scale, Request::Crop crop)
{
	switch (atoi(image.attribute("EXIF:Orientation").c_str())) {
	case Magick::TopLeftOrientation:
		break;

	case Magick::TopRightOrientation:
		image.flop();
		break;

	case Magick::BottomRightOrientation:
		image.rotate(180);
		break;

	case Magick::BottomLeftOrientation:
		image.flip();
		break;

	case Magick::LeftTopOrientation:
		image.rotate(90);
		image.flop();
		break;

	case Magick::RightTopOrientation:
		image.rotate(90);
		break;

	case Magick::RightBottomOrientation:
		image.rotate(270);
		image.flop();
		break;

	case Magick::LeftBottomOrientation:
		image.rotate(270);
		break;
	}

	Magick::Geometry size = image.size();
	int width = size.width();
	int height = size.height();

	switch (crop) {
	case Request::NO_CROP:
		break;

	case Request::TOP_SQUARE:
		if (width < height) {
			image.crop(Magick::Geometry(width, width));
			height = width;
		} else {
			image.crop(Magick::Geometry(height, height, (width - height) / 2, 0));
			width = height;
		}
		break;
	}

	if (width > scale || height > scale)
		image.scale(Magick::Geometry(scale, scale));

	image.strip();
}

static void free_blob_data(void *, void *hint)
{
	Magick::Blob *blob = reinterpret_cast<Magick::Blob *> (hint);
	delete blob;
}

static void write_jpeg(Magick::Image &image, zmq::message_t &data)
{
	std::auto_ptr<Magick::Blob> blob(new Magick::Blob);
	image.write(blob.get(), "JPEG");
	data.rebuild(const_cast<void *> (blob->data()), blob->length(), free_blob_data, blob.get());
	blob.release();
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s ADDRESS...\n", argv[0]);
		return 2;
	}

	const char *progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	openlog(progname, LOG_CONS | LOG_NDELAY | LOG_PID, LOG_USER);

	Magick::InitializeMagick(argv[0]);

	zmq::context_t context(1);
	zmq::socket_t socket(context, ZMQ_REP);

	try {
		for (int i = 1; i < argc; i++)
			socket.bind(argv[i]);

		while (true) {
			IO io(socket);

			if (!io.receive()) {
				syslog(LOG_ERR, "request message was too short");
				continue;
			}

			Request request;
			Response response;

			if (!decode_request(io.request_header, request)) {
				syslog(LOG_ERR, "could not decode request header");
				continue;
			}

			try {
				Magick::Blob blob(io.request_image.data(), io.request_image.size());
				Magick::Image image(blob);

				response.set_original_format(image.magick());
				convert_image(image, request.scale(), request.crop());
				write_jpeg(image, io.response_image);
			} catch (const Magick::Exception &e) {
				syslog(LOG_ERR, "magick: %s", e.what());
				continue;
			}

			encode_response(response, io.response_header);
			io.handled = true;
		}
	} catch (const zmq::error_t &e) {
		syslog(LOG_CRIT, "zmq: %s", e.what());
	}

	return 1;
}
