#ifndef THUMQ_IO_HPP
#define THUMQ_IO_HPP

#include <utility>

#include <zmq.hpp>

namespace thumq {

class IO
{
	IO(const IO &) = delete;
	IO &operator=(const IO &) = delete;

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
	 * complete message if handled, or an incomplete message if not.
	 */
	~IO() throw (zmq::error_t)
	{
		if (!m_received)
			return;

		send_part(response.first, handled);

		if (handled)
			send_part(response.second, false);
	}

	/**
	 * @return true if a complete message was received, or false if an
	 *         incomplete message was received.
	 */
	bool receive()
	{
		if (receive_part(request.first)) {
			if (receive_part(request.second)) {
				zmq::message_t extraneous;

				while (receive_part(extraneous)) {
				}
			}

			return true;
		}

		return false;
	}

	std::pair<zmq::message_t, zmq::message_t> request;
	std::pair<zmq::message_t, zmq::message_t> response;
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
