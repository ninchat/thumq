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

#include <seccomp.h>

#include <zmq.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <magic.h>

#include <Magick++.h>

#include "io.hpp"
#include "thumq.pb.h"

namespace protobuf = google::protobuf;

using namespace thumq;

namespace {

static magic_t magic_cookie;

static int init_seccomp()
{
	scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
	if (ctx == nullptr)
		return -1;

	int retval = -1;

	if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(access), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_gettime), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(times), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0) == 0 &&
	    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOMEM), SCMP_SYS(mmap), 0) == 0 &&
	    seccomp_load(ctx) == 0) {
		retval = 0;
	}

	seccomp_release(ctx);
	return retval;
}

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
	if (init_seccomp() < 0)
		return 1;

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

	auto image_data = reinterpret_cast<const char *> (request_data.data()) + 4 + header_size;
	const char *mimetype = magic_buffer(magic_cookie, image_data, request.length());
	Response response;
	Magick::Blob nail_blob;

	if (strcmp(mimetype, "image/bmp") == 0 ||
	    strcmp(mimetype, "image/gif") == 0 ||
	    strcmp(mimetype, "image/jpeg") == 0 ||
	    strcmp(mimetype, "image/png") == 0) {
		Magick::Blob orig_blob(image_data, request.length());
		Magick::Image image(orig_blob);

		response.set_original_format(image.magick());
		convert_image(image, request.scale(), request.crop());
		response.set_width(image.size().width());
		response.set_height(image.size().height());
		image.write(&nail_blob, "JPEG");
	}

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
			_exit(99);
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

	magic_cookie = magic_open(MAGIC_MIME_TYPE);
	if (magic_load(magic_cookie, nullptr) < 0) {
		syslog(LOG_CRIT, "magic: %s", strerror(errno));
		return 1;
	}

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
