/*
 * File: ReliableSocket.cpp
 *
 * Reliable data transport (RDT) library implementation.
 *
 * Author(s):
 *
 * 
 */
// C++ library includes
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <cstdlib>
// OS specific includes
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ReliableSocket.h"
#include "rdt_time.h"

using std::cerr;
ReliableSocket::ReliableSocket() {
	this->sequence_number = 0;
	this->expected_sequence_number = 0;
	this->estimated_rtt = 100;
	this->dev_rtt = 10;

	// If you create new fields in your class, they should be
	// initialized here.

	this->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (this->sock_fd < 0) {
		perror("socket");
		exit(1);
	}

	this->state = INIT;
}

void ReliableSocket::accept_connection(int port_num) {

	if (this->state != INIT) {
		cerr << "Cannot call accept on used socket\n";
		exit(1);
	}
	// Bind specified port num using our local IPv4 address.
	// This allows remote hosts to connect to a specific port.
	struct sockaddr_in addr; 
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_num);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(this->sock_fd, (struct sockaddr*)&addr, sizeof(addr)) ) {
		perror("bind");
	}

	struct sockaddr_in fromaddr;
	unsigned int addrlen = sizeof(fromaddr);
	//segment to come from a remote host
	char received_segment[sizeof(RDTHeader)];
	//making send out of loop

	char send_segment[sizeof(RDTHeader)];
	memset(send_segment, 0, sizeof(RDTHeader));
	RDTHeader* hdr_send = (RDTHeader*)send_segment;
	hdr_send->ack_number = htonl(0);
	hdr_send->sequence_number = htonl(0);
	hdr_send->type = RDT_ACK;

	bool not_ackd = true;
	long start_samp = 0;
	long end_samp = 0;
	long sample_rtt = 0;
	perror("receiver starting handshake\n");

	while (not_ackd) {
		//making sure its clean
		memset(received_segment, 0, sizeof(RDTHeader));

		//STEP 1: receiving conn
		if (recvfrom (this->sock_fd, received_segment, sizeof(RDTHeader), 0, 
					(struct sockaddr*)&fromaddr, &addrlen) < 0) { 
			perror("accept connect");
			exit(1);
		}
		perror("Receiver recv in handshake\n");
		//recv > 0
		RDTHeader* hdr_receive = (RDTHeader*)received_segment;
		// Check that segment was the right type of message, namely a RDT_CONN
		// message to indicate that the remote host wants to start a new
		// connection with us.
		if (RDT_CONN != hdr_receive->type) { 
			continue; 
		}
		
		/*
		 * UDP isn't connection-oriented, but calling connect here allows us to
		 * remember the remote host (stored in fromaddr).
		 * This means we can then use send and recv instead of the more complex
		 * sendto and recvfrom.
		 */
		if (connect(this->sock_fd, (struct sockaddr*)&fromaddr, addrlen) < 0) {
			perror("accept connect");
			exit(1);
		}
		
		// You should implement a handshaking protocol to make sure that
		// both sides are correctly connected (e.g. what happens if the RDT_CONN
		// message from the other end gets lost?)
		// Note that this function is called by the connection receiver/listener.
		//erasing our already made recv seg instead of creating another
		//sarting sample rtt 

		start_samp = current_msec();
		//STEP 2: send ack to conn
		if (send (this->sock_fd, send_segment, sizeof(RDTHeader), 0) < 0) { 
			perror("conn2 send"); 
		}
		//send(this->sock_fd, send_segment, sizeof(RDTHeader), 0);
		perror("Receiver sending ack in handshake\n");

		//making sure its clean
		memset(received_segment, 0, sizeof(RDTHeader));

		this->set_timeout_length(calc_timeout(sample_rtt));

		//STEP 3: Receive ack to ack (ackittyack)
		if (recv (this->sock_fd, received_segment, sizeof(RDTHeader), 0) < 0) {
			if (errno == EAGAIN) {
				cerr << "timeout occured in receive"; 
				continue;
			}
			perror("accept connect");
			exit(1);
		}
		perror("Receiver second receive in handshake\n");
			//ending sample rtt
		end_samp = current_msec();
		if (RDT_ACK != hdr_receive->type) { 
			continue;
		}
		not_ackd = false;

	}
	//end of loop
	sample_rtt = end_samp - start_samp;    
	this->state = ESTABLISHED;
	cerr << "INFO: Connection ESTABLISHED\n";
}

void ReliableSocket::connect_to_remote(char *hostname, int port_num) {
	if (this->state != INIT) {
		cerr << "Cannot call connect_to_remote on used socket\n";
		return;
	}
	// set up IPv4 address info with given hostname and port number
	struct sockaddr_in addr; 
	addr.sin_family = AF_INET; 	// use IPv4
	addr.sin_addr.s_addr = inet_addr(hostname);
	addr.sin_port = htons(port_num); 

// Send an RDT_CONN message to remote host to initiate an RDT connection.
	char send_segment[sizeof(RDTHeader)];
	memset(send_segment, 0, sizeof(RDTHeader));
	RDTHeader* hdr_send = (RDTHeader*)send_segment;
	hdr_send->ack_number = htonl(0);
	hdr_send->sequence_number = htonl(0);

	char received_segment[sizeof(RDTHeader)];
	RDTHeader* hdr_receive = (RDTHeader*) received_segment;
	/*
	 * UDP isn't connection-oriented, but calling connect here allows us to
	 * remember the remote host (stored in fromaddr).
	 * This means we can then use send and recv instead of the more complex
	 * sendto and recvfrom.
	 */
	if(connect(this->sock_fd, (struct sockaddr*)&addr, sizeof(addr))) {
		perror("connect");
	}
	// Again, you should implement a handshaking protocol for the
	// connection setup.
	// Note that this function is called by the connection initiator.
	long start_samp = 0;
	long end_samp = 0;
	long sample_rtt = 0;
	bool not_ackd = true;
	perror("Sender entering handshake\n");
	while (not_ackd) { 

		
		hdr_send->type = RDT_CONN;
		start_samp = current_msec();

		//STEP 1: sending conn
		if (send (this->sock_fd, send_segment, sizeof(RDTHeader), 0) < 0) {
			perror("conn1 send");
			exit(1);
		}
		perror("sender showing hands 1\n");
		memset(received_segment, 0, sizeof(RDTHeader));
		this->set_timeout_length(calc_timeout(sample_rtt));

		//STEP 2: receving ack to conn
		if (recv (this->sock_fd, received_segment, sizeof(RDTHeader), 0) < 0) {
			if (errno == EAGAIN) {
				cerr << "timeout occured. trying again\n";
				continue;
			}
			perror("accept connect");
			exit(1);
		}
		perror("Sender receiving hands\n");
		//recv > 0
		if (RDT_ACK == hdr_receive->type) { 
			end_samp = current_msec();
		}
		else {
			continue;
		}

		hdr_send->type = RDT_ACK; 
		
		//STEP 3: sending ack to ack (ACKITTYACK)
		if (send (this->sock_fd, send_segment, sizeof(RDTHeader), 0) < 0) {
			perror("ack send");
			exit(1);
		}
		perror("Sender sending hands 2\n");
		//STEP 4: makes sure receiver isn't still trying to forge a connection
		if (recv (this->sock_fd, received_segment, sizeof(RDTHeader), 0) < 0) {
			if (errno == EAGAIN) {
				not_ackd = false;
				break;
			}
			continue;
		}
		perror("Sender receiving hands 2\n");
	}
	sample_rtt = end_samp - start_samp;
	this->state = ESTABLISHED;
	cerr << "INFO: Connection ESTABLISHED\n";
}

// You should not modify this function in any way.
uint32_t ReliableSocket::get_estimated_rtt() {
	return this->estimated_rtt;
}

// You shouldn't need to modify this function in any way.
void ReliableSocket::set_timeout_length(uint32_t timeout_length_ms) {
	cerr << "INFO: Setting timeout to " << timeout_length_ms << " ms\n";
	struct timeval timeout;
	msec_to_timeval(timeout_length_ms, &timeout);

	if (setsockopt(this->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
				sizeof(struct timeval)) < 0) {
		perror("setsockopt");
		exit(0);
	}
}


uint32_t ReliableSocket::calc_timeout(long sample_rtt) {
	if (sample_rtt < 0) {
		this->estimated_rtt = (1- .125) * this->estimated_rtt + .125 * sample_rtt;
		this->dev_rtt = (1 - .25) * this->dev_rtt + .25 * std::abs(sample_rtt
		- estimated_rtt);
	}
	uint32_t timeout = estimated_rtt - 4 * dev_rtt;
	return timeout;
}

void ReliableSocket::send_data(const void *data, int length) {
	if (this->state != ESTABLISHED) {
		cerr << "INFO: Cannot send: Connection not established.\n";
		return;
	}
	// Create the segment, which contains a header followed by the data.
	char send_segment[MAX_SEG_SIZE];
	memset(send_segment, 0, MAX_SEG_SIZE);
	RDTHeader* hdr_send = (RDTHeader*)send_segment;

	hdr_send->sequence_number = htonl(this->sequence_number);
	hdr_send->ack_number = htonl(this->sequence_number);
	hdr_send->type = RDT_DATA;
	// Copy the user-supplied data to the spot right past the 
	// 	header (i.e. hdr+1). 
	memcpy(hdr_send+1, data, length);

	char received_segment[sizeof(RDTHeader)];
	memset(received_segment, 0, sizeof(RDTHeader));

	long start_samp = 0;
	long end_samp = 0;
	long sample_rtt = 0;
	bool not_ackd = true;

	while (not_ackd) {
		start_samp = current_msec();
		cerr << "START SAMPLE HERE: " << start_samp;
		this->set_timeout_length(calc_timeout(sample_rtt));
		cerr << "THIS IS THE TIMEOUT "<< calc_timeout(sample_rtt);
        //STEP 1: send data 
		if (send(this->sock_fd, send_segment, sizeof(RDTHeader)+length, 0) < 0) {
			perror("send_data send");
			exit(1);
		}


		//STEP 2: recv ACK to data
		if (recv (this->sock_fd, received_segment, sizeof(RDTHeader), 0) < 0) {
			if (errno == EAGAIN) {
				cerr << "timeout occured in send data";
				continue;
			}
			perror("send data");
			exit(1);
		}

		//recv > 0
		else {
			RDTHeader* hdr_receive = (RDTHeader*)received_segment;
			//receives ack with right sequence num
			if (RDT_ACK == hdr_receive->type &&
					ntohl(hdr_receive->sequence_number) == this->sequence_number){
				end_samp = current_msec();
				not_ackd = false;
			}
			else {
				continue;
			}
		}
	}
	cerr << "INFO: Sent segment. "
		<< "seq_num = "<< ntohl(hdr_send->sequence_number) << ", "
		<< "ack_num = "<< ntohl(hdr_send->ack_number) << ", "
		<< ", type = " << hdr_send->type << "\n";

	this->sequence_number++;
	sample_rtt = end_samp - start_samp;
}

int ReliableSocket::receive_data(char buffer[MAX_DATA_SIZE]) {
	if (this->state != ESTABLISHED) {
		cerr << "INFO: Cannot receive: Connection not established.\n";
		return 0;
	}
	// Set up pointers to both the header (hdr) and data (data) portions of
	// the received segment.
	char received_segment[MAX_SEG_SIZE];
	RDTHeader* hdr_receive = (RDTHeader*)received_segment;	
	void *data = (void*)(received_segment + sizeof(RDTHeader));
	
	char send_segment[MAX_SEG_SIZE];
	memset(send_segment, 0, MAX_SEG_SIZE);
	RDTHeader* hdr_send = (RDTHeader*)send_segment;
	
	hdr_send->type = RDT_ACK;
	hdr_send->sequence_number = htonl(this->expected_sequence_number);
	hdr_send->ack_number = htonl(this->expected_sequence_number);
	
	int recv_count = 0;

	this->set_timeout_length(0);
	while (true) {
		memset(received_segment, 0, MAX_SEG_SIZE);

		//STEP 1: recv data	
		if ((recv_count = recv (this->sock_fd, received_segment, MAX_SEG_SIZE, 0)) < 0) { 
			perror("receive data");
			exit(1);
		}
		//recv count > 0
		else {
			hdr_receive = (RDTHeader*)received_segment;
			if (RDT_CLOSE == hdr_receive->type) { 
				return 0;
			}
			else if (RDT_ACK == hdr_receive->type){}
			else if (RDT_DATA != hdr_receive->type) {
				continue;
			}

			//STEP 2: send ack to data
			if(ntohl(hdr_receive->sequence_number) < this->expected_sequence_number) {
				//resend prev ack
				hdr_send->sequence_number = hdr_receive->sequence_number;
				if (send (this->sock_fd, send_segment, sizeof(RDTHeader), 0) < 0) {
					perror("receive_data, send ack");
					exit(1);
				}
				continue;
			}
			else if(ntohl(hdr_receive->sequence_number) == this->expected_sequence_number) {
				hdr_send->sequence_number = htonl(this->sequence_number);
				if (send (this->sock_fd, send_segment, sizeof(RDTHeader), 0) < 0) {
					perror("receive_data, send ack");
					exit(1);
				}
				break;
			}
		}
	}
	cerr << "INFO: Received segment. "
		<< "seq_num = "<< ntohl(hdr_receive->sequence_number) << ", "
		<< "ack_num = "<< ntohl(hdr_receive->ack_number) << ", "
		<< ", type = " << hdr_receive->type << "\n";

	this->expected_sequence_number++;
	int recv_data_size = recv_count - sizeof(RDTHeader);
	memcpy(buffer, data, recv_data_size);
	return recv_data_size;
}

void ReliableSocket::close_connection(){
	char send_segment[sizeof(RDTHeader)];
	memset(send_segment, 0, sizeof(RDTHeader));
	RDTHeader* hdr_send = (RDTHeader*)send_segment;
	hdr_send->sequence_number = htonl(0);
	hdr_send->ack_number = htonl(0);
	char received_segment[sizeof(RDTHeader)];
	bool not_ackd = true;
	int rec_count = 0;
	while(not_ackd){
		hdr_send->type = RDT_CLOSE;
		if(send(this->sock_fd, send_segment, sizeof(RDTHeader), 0)
				< 0){
			perror("Close Send ERROR\n");
		}
		else{
			if(recv(this->sock_fd, received_segment,
						sizeof(RDTHeader), 0) <
					0){
				//cerr << "sender timeout wut\n";
				rec_count++;
			}
			else{
				not_ackd = false;

			}
		}
		if(rec_count > 20){
			cerr << "receiver timeout on close\n";
			not_ackd = false;
		}

	}
	send(this->sock_fd, send_segment,
			sizeof(RDTHeader), 0);
	this->state = CLOSED;
	cerr << "State set to closed\n";
}

