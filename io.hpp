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
		m_socket(socket),
		m_received(false)
	{
	}

	/**
	 * Sends a message if a message has been received.
	 */
	~IO() noexcept(false) // throw (zmq::error_t)
	{
		if (m_received)
			m_socket.send(response);
	}

	void receive()
	{
		if (receive_part(request)) {
			zmq::message_t extraneous;
			while (receive_part(extraneous)) {
			}
		}
		m_received = true;
	}

	zmq::message_t request;
	zmq::message_t response;

private:
	bool receive_part(zmq::message_t &message)
	{
		m_socket.recv(&message);
		int more;
		size_t length = sizeof more;
		m_socket.getsockopt(ZMQ_RCVMORE, &more, &length);
		return more;
	}

	zmq::socket_t &m_socket;
	bool m_received;
};

} // namespace thumq

#endif
