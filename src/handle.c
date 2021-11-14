/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "ring_buffer.h"
#include "tinytcp.h"
#include "handle.h"

int send_now = 1;

// char ** old_arr;

// uint32_t ring_buffer_read(ring_buffer_t * buffer, char* dst_buff, uint32_t bytes);

// struct ring_buffer;
// typedef struct ring_buffer ring_buffer_t;

void* handle_send_to_network(void* args)
{

    fprintf(stderr, "### started send thread\n");
    // int size = tinytcp_conn_list_size;
    // old_arr = malloc(size * sizeof(char*));
    // for (int i = 0; i < size; ++i)
    // {
    //     old_arr[i] = (char *)calloc(MSS + 1, sizeof(char));
    // }

    // char * old_data = (char *)calloc(MSS + 1, sizeof(char));

    while (1) {

        int call_send_to_network = 0;

        for (int i = 0; i < tinytcp_conn_list_size; ++i) {
            tinytcp_conn_t* tinytcp_conn = tinytcp_conn_list[i];

            if (tinytcp_conn->curr_state == CONN_ESTABLISHED
            || tinytcp_conn->curr_state == READY_TO_TERMINATE) {

                if (tinytcp_conn->curr_state == READY_TO_TERMINATE) {
                    if (occupied_space(tinytcp_conn->send_buffer, NULL) == 0) {
                        handle_close(tinytcp_conn);
                        num_of_closed_conn++;
                        continue;
                    }
                }

                if (timer_expired(tinytcp_conn->time_last_new_data_acked)) {
                    //TODO resend packets after last packets acked
                    
                    char * data = malloc(sizeof(char) * (MSS + 1));
                    uint32_t data_size = ring_buffer_read(tinytcp_conn->send_buffer, data, MSS);
                    char* data_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
                    tinytcp_conn->dst_port, tinytcp_conn->seq_num,
                    tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
                    send_to_network(data_pkt, TINYTCP_HDR_SIZE + data_size);
                    tinytcp_conn->time_last_new_data_acked = clock();
                    free(data);
                }

                //TODO do something else
                while(occupied_space(tinytcp_conn->send_buffer, NULL) < MSS && tinytcp_conn->curr_state != READY_TO_TERMINATE);
                char * data = malloc(sizeof(char) * (MSS + 1));
                int i = 0;
                uint32_t data_size = ring_buffer_read(tinytcp_conn->send_buffer, data, MSS);

                if(data_size > 0 && send_now == 1) //&& memcmp(old_data, data, MSS) != 0)  //only client sends and only send new data
                {
                    tinytcp_conn->seq_num += MSS;
                    char* data_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
                    tinytcp_conn->dst_port, tinytcp_conn->seq_num,
                    tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
                    send_to_network(data_pkt, TINYTCP_HDR_SIZE + data_size);
                    // memcpy(old_data, data, MSS);
                    send_now = 0;
                    //strcpy(old_data, data);
                    //TODO make call_send_to_network = 1 everytime you make a call to send_to_network()
                    call_send_to_network = 1;
                }
                free(data);
            }
        }

        if (call_send_to_network == 0) {
            usleep(100);
        }
    }
    // for (int i = 0; i < size; ++i)
    // {
    //     free(old_arr[i]);
    // }
    // free(old_arr);

    // free(old_data);
    
}


void handle_recv_from_network(char* tinytcp_pkt,
                              uint16_t tinytcp_pkt_size)
{
    //parse received tinytcp packet
    tinytcp_hdr_t* tinytcp_hdr = (tinytcp_hdr_t *) tinytcp_pkt;

    uint16_t src_port = ntohs(tinytcp_hdr->src_port);
    uint16_t dst_port = ntohs(tinytcp_hdr->dst_port);
    uint32_t seq_num = ntohl(tinytcp_hdr->seq_num);
    uint32_t ack_num = ntohl(tinytcp_hdr->ack_num);
    uint16_t data_offset_and_flags = ntohs(tinytcp_hdr->data_offset_and_flags);
    uint8_t tinytcp_hdr_size = ((data_offset_and_flags & 0xF000) >> 12) * 4; //bytes
    uint8_t ack = (data_offset_and_flags & 0x0010) >> 4;
    uint8_t syn = (data_offset_and_flags & 0x0002) >> 1;
    uint8_t fin = data_offset_and_flags & 0x0001;
    char* data = tinytcp_pkt + TINYTCP_HDR_SIZE;
    uint16_t data_size = tinytcp_pkt_size - TINYTCP_HDR_SIZE;

    if (syn == 1 && ack == 0) { //SYN recvd
        //create tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_create_conn();

        //TODO initialize tinytcp_conn attributes. filename is contained in data
        memcpy(tinytcp_conn->filename, data, data_size);
        tinytcp_conn->curr_state = SYN_RECVD;
        tinytcp_conn->src_port = dst_port;
        tinytcp_conn->dst_port = src_port;
        tinytcp_conn->seq_num = rand();
        tinytcp_conn->send_buffer = create_ring_buffer(0);
        tinytcp_conn->recv_buffer = create_ring_buffer(0);
        // tinytcp_conn->eof = 0;

        char filepath[500];
        strcpy(filepath, "recvfiles/");
        strncat(filepath, data, data_size);
        strcat(filepath, "\0");

        tinytcp_conn->r_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        assert(tinytcp_conn->r_fd >= 0);

        fprintf(stderr, "\nSYN recvd "
                "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                src_port, dst_port, seq_num, ack_num);

        //TODO update tinytcp_conn attributes
        tinytcp_conn->ack_num = seq_num + 1;
        tinytcp_conn->curr_state = SYN_ACK_SENT;

        fprintf(stderr, "\nSYN-ACK sending "
                "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                tinytcp_conn->src_port, tinytcp_conn->dst_port,
                tinytcp_conn->seq_num, tinytcp_conn->ack_num);

        //TODO send SYN-ACK
        data = '\0';
        data_size = 0;
        char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 1, 0, data, data_size);
        send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

    } else if (syn == 1 && ack == 1) { //SYN-ACK recvd

        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == SYN_SENT) {
            //TODO update tinytcp_conn attributes
            //SYN_ACK received
            tinytcp_conn->curr_state = SYN_ACK_RECVD;
            tinytcp_conn->ack_num = seq_num + 1;

            fprintf(stderr, "\nSYN-ACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

        }

    } else if (fin == 1 && ack == 1) {
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == CONN_ESTABLISHED) { //FIN recvd
            tinytcp_conn->curr_state = FIN_RECVD;
            fprintf(stderr, "\nFIN recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            //flush the recv_buffer
            while (occupied_space(tinytcp_conn->recv_buffer, NULL) != 0) {
                usleep(10);
            }

            //TODO update tinytcp_conn attributes
            tinytcp_conn->curr_state = FIN_ACK_SENT;
            tinytcp_conn->seq_num = ack_num;
            tinytcp_conn->ack_num = seq_num + 1;
            
            fprintf(stderr, "\nFIN-ACK sending "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    tinytcp_conn->src_port, tinytcp_conn->dst_port,
                    tinytcp_conn->seq_num, tinytcp_conn->ack_num);

            //TODO send FIN-ACK
            char * fin_ack_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 1, data, data_size);
            send_to_network(fin_ack_pkt, TINYTCP_HDR_SIZE + data_size);

        } else if (tinytcp_conn->curr_state == FIN_SENT) { //FIN_ACK recvd
            //TODO update tinytcp_conn attributes
            tinytcp_conn->curr_state = FIN_ACK_RECVD;
            tinytcp_conn->ack_num = seq_num + 1;


            fprintf(stderr, "\nFIN-ACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

        }

    } else if (ack == 1) {
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == SYN_ACK_SENT) { //conn set up ACK
            //TODO update tinytcp_conn attributes
            tinytcp_conn->ack_num += MSS;
            tinytcp_conn->curr_state = CONN_ESTABLISHED;
            fprintf(stderr, "\nACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            fprintf(stderr, "\nconnection established...receiving file %s\n\n",
                    tinytcp_conn->filename);

        } else if (tinytcp_conn->curr_state == FIN_ACK_SENT) { //conn terminate ACK
            //TODO update tinytcp_conn attributes
            tinytcp_conn->curr_state = CONN_TERMINATED;

            fprintf(stderr, "\nACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            tinytcp_free_conn(tinytcp_conn);

            fprintf(stderr, "\nfile %s received...connection terminated\n\n",
                    tinytcp_conn->filename);

        } else if (tinytcp_conn->curr_state == CONN_ESTABLISHED
            || tinytcp_conn->curr_state == READY_TO_TERMINATE) { //data ACK
            //implement this only if you are sending any data.. not necessary for
            //initial parts of the assignment!

            //TODO handle received data packets
            if (data_size > 0) // data received from client
            {
                // if (seq_num == tinytcp_conn->ack_num) //if packet received is the expected
                // {
                // did_add = 1; 
                // add data received to receive buffer and send ack
                while(data_size > empty_space(tinytcp_conn->recv_buffer)); // if not enough space in the receive buffer wait for more empty space
                uint32_t bytes = ring_buffer_add(tinytcp_conn->recv_buffer, data, data_size);

                //TODO send back an ACK (if needed).
                if (bytes > 0)
                {
                    data = '\0';
                    data_size = 0;
                    tinytcp_conn->ack_num += MSS; 
                    char * ack_pkt = create_tinytcp_pkt(tinytcp_conn->src_port, tinytcp_conn->dst_port, tinytcp_conn->seq_num, tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
                    send_to_network(ack_pkt, TINYTCP_HDR_SIZE + data_size);
                }
                // }
                // else
                // {
                //     data = '\0';
                //     data_size = 0;
                //     char * ack_pkt = create_tinytcp_pkt(tinytcp_conn->src_port, tinytcp_conn->dst_port, tinytcp_conn->seq_num, tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
                //     send_to_network(ack_pkt, TINYTCP_HDR_SIZE + data_size);
                // }    
            }
            else
            {
                // if (ack_num <= tinytcp_conn->seq_num)
                // {
                //     tinytcp_conn->num_of_dup_acks += 1;
                // }
                // else
                // {
                // tinytcp_conn->num_of_dup_acks = 0;
                char * dst_buffer = malloc(sizeof(char) * (MSS + 1));
                uint32_t bytes_read = ring_buffer_remove(tinytcp_conn->send_buffer, dst_buffer, MSS);
                free(dst_buffer);
                send_now = 1;
                // }
                
                //TODO reset timer (i.e., set time_last_new_data_acked to clock())
                //every time some *new* data has been ACKed
                // tinytcp_conn->ack_num = seq_num + 1;

                // resets timer whenever NEW data is ACKED
                // if (ack_num == tinytcp_conn->seq_num)
                // {
                tinytcp_conn->time_last_new_data_acked = clock();
                // }
                
            }

        }
    }
}


int tinytcp_connect(tinytcp_conn_t* tinytcp_conn,
                    uint16_t cliport, //use this to initialize src port
                    uint16_t servport, //use this to initialize dst port
                    char* data, //filename is contained in the data
                    uint16_t data_size)
{
    //TODO initialize tinytcp_conn attributes. filename is contained in data
    tinytcp_conn->src_port = cliport;
    tinytcp_conn->dst_port = servport;
    memcpy(tinytcp_conn->filename, data, data_size);
    tinytcp_conn->seq_num = rand();
    tinytcp_conn->send_buffer = create_ring_buffer(0);
    tinytcp_conn->recv_buffer = create_ring_buffer(0);
    // tinytcp_conn->eof = 0;



    fprintf(stderr, "\nSYN sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    //send SYN, put data (filename) into the packet
    char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 0, 1, 0, data, data_size);
    
    //change state to SYN_SENT
    tinytcp_conn->curr_state = SYN_SENT;
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

    //wait for SYN-ACK
    while (tinytcp_conn->curr_state != SYN_ACK_RECVD) {
        usleep(10);
    }

    //TODO update tinytcp_conn attributes
    tinytcp_conn->seq_num += 1;
    tinytcp_conn->curr_state = CONN_ESTABLISHED;
    fprintf(stderr, "\nACK sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    //TODO send ACK
    data = '\0';
    data_size = 0;
    tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

    fprintf(stderr, "\nconnection established...sending file %s\n\n",
            tinytcp_conn->filename);

    return 0;
}


void handle_close(tinytcp_conn_t* tinytcp_conn)
{
    char * data = '\0';
    uint16_t data_size = 0;
    //TODO update tinytcp_conn attributes
    tinytcp_conn->seq_num += 1;
    tinytcp_conn->curr_state = FIN_SENT;

    fprintf(stderr, "\nFIN sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    //TODO send FIN
    char * fin_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 1, data, data_size);
    send_to_network(fin_pkt, TINYTCP_HDR_SIZE + data_size);
    
    //wait for FIN-ACK
    while (tinytcp_conn->curr_state != FIN_ACK_RECVD) {
        usleep(10);
    }

    //TODO update tinytcp_conn attributes
    tinytcp_conn->curr_state = CONN_TERMINATED;
    tinytcp_conn->seq_num += 1;

    fprintf(stderr, "\nACK sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    //TODO send ACK
    char * ack_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
    send_to_network(ack_pkt, TINYTCP_HDR_SIZE + data_size);
    
    tinytcp_free_conn(tinytcp_conn);

    fprintf(stderr, "\nfile %s sent...connection terminated\n\n",
            tinytcp_conn->filename);

    return;
}



// uint32_t ring_buffer_read(ring_buffer_t * buffer, char* dst_buff, uint32_t bytes)
// {
//     if (buffer == NULL) {
//         fprintf(stderr, "error ring_buffer_read: buffer is NULL\n");
//         exit(1);
//     }

//     uint32_t occupied = occupied_space(buffer, NULL);
//     if (bytes > occupied) {
//         bytes = occupied;
//     }

//     uint32_t start_idx = buffer->head % buffer->capacity;
//     if (dst_buff != NULL && bytes > 0) {
//         if (start_idx + bytes <= buffer->capacity) {
//             memcpy(dst_buff, (buffer->data + start_idx), bytes);
//         } else {
//             uint32_t diff = bytes - (buffer->capacity - start_idx);
//             memcpy(dst_buff, (buffer->data + start_idx),
//                     (buffer->capacity - start_idx));
//             memcpy(dst_buff + (buffer->capacity - start_idx), buffer->data, diff);
//         }
//     }
//     return bytes; 
// }
