#ifndef THUMQ_IO_HPP
#define THUMQ_IO_HPP

#include <zmq.hpp>

namespace thumq {

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

} // namespace thumq

#endif
