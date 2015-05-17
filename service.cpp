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

static bool receive_message_part(zmq::socket_t &socket, zmq::message_t &message)
{
	while (true) {
		try {
			socket.recv(&message);
			break;
		} catch (const zmq::error_t &e) {
			if (e.num() != EINTR)
				throw e;
		}
	}

	while (true) {
		try {
			int value;
			size_t length = sizeof value;
			socket.getsockopt(ZMQ_RCVMORE, &value, &length);
			return value;
		} catch (const zmq::error_t &e) {
			if (e.num() != EINTR)
				throw e;
		}
	}
}

static void send_message_part(zmq::socket_t &socket, zmq::message_t &message, bool more)
{
	int flags = 0;

	if (more)
		flags |= ZMQ_SNDMORE;

	while (true) {
		try {
			socket.send(message, flags);
			break;
		} catch (const zmq::error_t &e) {
			if (e.num() != EINTR)
				throw e;
		}
	}
}

static bool decode_request(const zmq::message_t &message, Request &request)
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

	zmq::context_t context;
	zmq::socket_t socket(context, ZMQ_REP);

	for (int i = 1; i < argc; i++)
		socket.bind(argv[i]);

	while (true) {
		zmq::message_t request_data;
		zmq::message_t input_image_data;
		Response response;
		zmq::message_t output_image_data;

		if (receive_message_part(socket, request_data)) {
			if (receive_message_part(socket, input_image_data)) {
				zmq::message_t extraneous;
				while (receive_message_part(socket, extraneous)) {
				}
			}

			Request request;

			if (decode_request(request_data, request)) {
				try {
					Magick::Blob blob(input_image_data.data(), input_image_data.size());
					Magick::Image image(blob);

					response.set_original_format(image.magick());
					convert_image(image, request.scale(), request.crop());
					write_jpeg(image, output_image_data);
				} catch (const Magick::Exception &e) {
					syslog(LOG_ERR, "%s", e.what());
				}
			} else {
				syslog(LOG_ERR, "could not decode request header");
			}
		} else {
			syslog(LOG_ERR, "request message was too short");
		}

		zmq::message_t response_data;
		encode_response(response, response_data);

		if (output_image_data.size() > 0) {
			send_message_part(socket, response_data, true);
			send_message_part(socket, output_image_data, false);
		} else {
			send_message_part(socket, response_data, false);
		}
	}
}
