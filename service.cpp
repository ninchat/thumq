#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <zmq.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <Magick++.h>

#include "io.hpp"
#include "thumq.pb.h"

namespace protobuf = google::protobuf;

using namespace thumq;

namespace {

static bool decode_request(const char *data, size_t size, Request &request)
{
	protobuf::io::ArrayInputStream array(data, size);
	protobuf::io::CodedInputStream coded(&array);

	return request.MergePartialFromCodedStream(&coded);
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

	switch (int(crop)) {
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

static void write_full(int fd, const void *data, ssize_t size)
{
	for (ssize_t len = 0; len < size; ) {
		ssize_t n = write(fd, reinterpret_cast<const char *> (data) + len, size - len);
		if (n < 0)
			throw strerror(errno);

		if (n == 0)
			throw "EOF while writing";

		len += n;
	}
}

static int child_process(const zmq::message_t &request_data, int response_fd)
{
	if (request_data.size() < 4)
		return 2;

	uint32_t header_size = *reinterpret_cast<const uint32_t *> (request_data.data()); // TODO
	if (header_size == 0 || header_size >= 0xffffff)
		return 3;

	if (request_data.size() < 4 + header_size)
		return 4;

	Request request;
	if (!decode_request(reinterpret_cast<const char *> (request_data.data()) + 4, header_size, request))
		return 5;

	if (request.length() != request_data.size() - 4 - header_size)
		return 6;

	Magick::Blob orig_blob(reinterpret_cast<const char *> (request_data.data()) + 4 + header_size, request.length());
	Magick::Image image(orig_blob);
	Response response;

	response.set_original_format(image.magick());
	convert_image(image, request.scale(), request.crop());
	response.set_width(image.size().width());
	response.set_height(image.size().height());

	Magick::Blob nail_blob;
	image.write(&nail_blob, "JPEG");
	response.set_length(nail_blob.length());

	char response_data[4 + response.ByteSize()];
	*reinterpret_cast<uint32_t *> (response_data) = response.ByteSize(); // TODO
	protobuf::io::ArrayOutputStream array(response_data + 4, response.ByteSize());
	protobuf::io::CodedOutputStream coded(&array);
	response.SerializeWithCachedSizes(&coded);

	write_full(response_fd, response_data, 4 + response.ByteSize());
	write_full(response_fd, nail_blob.data(), nail_blob.length());
	return 0;
}

static void handle(const zmq::message_t &request, zmq::message_t &response)
{
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0)
		throw strerror(errno);

	pid_t pid = fork();
	if (pid < 0)
		throw strerror(errno);

	if (pid == 0) {
		close(fds[0]);
		try {
			_exit(child_process(request, fds[1]));
		} catch (...) {
			_exit(1);
		}
	}

	close(fds[1]);

	static char buf[16 * 1024 * 1024];
	ssize_t len = 0;

	while (true) {
		ssize_t n = read(fds[0], buf + len, sizeof buf - len);
		if (n < 0)
			throw strerror(errno);

		if (n == 0)
			break;

		len += n;
		if (len >= (ssize_t) sizeof buf)
			throw "buffer overflow";
	}

	close(fds[0]);

	int status;
	if (waitpid(pid, &status, 0) < 0)
		throw strerror(errno);

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) == 0) {
			response.rebuild(len);
			memcpy(response.data(), buf, len);
		} else {
			syslog(LOG_ERR, "child process exted with code %d", WEXITSTATUS(status));
		}
	} else {
		syslog(LOG_ERR, "child process died");
	}

	memset(buf, 0, len);
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
			io.receive();
			handle(io.request, io.response);
		}
	} catch (const zmq::error_t &e) {
		syslog(LOG_CRIT, "zmq: %s", e.what());
	}

	return 1;
}
