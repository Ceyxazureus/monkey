/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2010, Eduardo Silva P.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "plugin.h"
#include "str.h"
#include "http.h"
#include "http_status.h"
#include "monkey.h"
#include "epoll.h"
#include "utils.h"
#include "header.h"

#include "cgi.h"
#include "palm.h"
#include "request.h"

/* Plugin data for register */
mk_plugin_data_t _shortname = "palm";
mk_plugin_data_t _name = "Palm";
mk_plugin_data_t _version = "0.2";
mk_plugin_hook_t _hooks = MK_PLUGIN_STAGE_10 | MK_PLUGIN_STAGE_40;

/* Read database configuration parameters */
int mk_palm_conf(char *confdir)
{
    int i, ret = 0;
    unsigned long len;
    char *conf_path;
    struct mk_config *p;
    struct mk_palm *new, *r;
    struct mk_string_line *line, *lp;

    mk_api->str_build(&conf_path, &len, "%s/palm.conf", confdir);

    p = conf = mk_api->config_create(conf_path);

    r = palms;
    while (p) {
        /* Validate palm configuration line */
        i = 0;
        if (strcasecmp(p->key, "Palm") == 0) {
            line = mk_api->str_split_line(p->val);
            lp = line;

            while (lp) {
                i++;
                if (lp->len <= 0) {
                    fprintf(stderr, MK_PALM_ERROR_LINE, p->val);
                    _exit(1);
                }
                lp = lp->next;
            }

            if (i != 4) {
                fprintf(stderr, MK_PALM_ERROR_LINE, p->val);
                _exit(1);
            }
        }
        lp = line;

        /* Alloc node */
        new = mk_api->mem_alloc(sizeof(struct mk_palm));

        /* Palm file extensions */
        new->extension = lp->val;
        lp = lp->next;

        /* Palm mime type */
        new->mimetype = lp->val;
        lp = lp->next;

        /* Palm server address */
        new->server_addr = lp->val;
        lp = lp->next;

        /* Palm server TCP port */
        new->server_port = atoi(lp->val);
        lp = lp->next;

        new->next = NULL;

        /* Linking node */
        if (!palms) {
            palms = new;
        }
        else {
            r = palms;
            while (r->next) {
                r = r->next;
            }
            r->next = new;
        }
        p = p->next;
    }

    mk_api->mem_free(conf_path);
    return ret;
}

struct mk_palm *mk_palm_get_handler(mk_pointer * file)
{
    struct mk_palm *p;
    int j, len, extlen;

    j = len = file->len;

    /* looking for extension */
    while (file->data[j] != '.' && j >= 0) {
        j--;
    }

    extlen = file->len - j - 1;
    if (j == 0) {
        return NULL;
    }

    p = palms;
    while (p) {
        if (strncasecmp(file->data + j + 1, p->extension, extlen) == 0) {
            return p;
        }
        p = p->next;
    }

    return NULL;
}

void mk_palm_iov_add_header(struct mk_iov *iov,
                            mk_pointer header, mk_pointer value)
{
    mk_api->iov_add_entry(iov, header.data, header.len,
                          mk_iov_equal, MK_IOV_NOT_FREE_BUF);
    mk_api->iov_add_entry(iov, value.data, value.len,
                          mk_iov_crlf, MK_IOV_NOT_FREE_BUF);
}

struct mk_iov *mk_palm_create_env(struct client_request *cr,
                                  struct request *sr)
{
    struct mk_iov *iov;

    iov = mk_api->iov_create(100, 0);
#ifdef TRACE
    PLUGIN_TRACE( "Create environment for palm server");
#endif
    mk_api->iov_add_entry(iov, sr->real_path.data,
                          sr->real_path.len, mk_iov_crlf, MK_IOV_NOT_FREE_BUF);

    mk_api->iov_add_entry(iov, mk_cgi_document_root.data,
                          mk_cgi_document_root.len,
                          mk_iov_equal, MK_IOV_NOT_FREE_BUF);

    mk_api->iov_add_entry(iov, sr->host_conf->documentroot.data,
                          sr->host_conf->documentroot.len, mk_iov_crlf,
                          MK_IOV_NOT_FREE_BUF);

    if (sr->method == HTTP_METHOD_POST && sr->content_length > 0) {
        /* Content length */
        mk_pointer p;
        unsigned long len;
        char *length = 0;
        mk_api->str_build(&length, &len, "%i", sr->content_length);
        p.data = length;
        p.len = len;

        mk_palm_iov_add_header(iov, mk_cgi_content_length, p);
        mk_palm_iov_add_header(iov, mk_cgi_content_type, sr->content_type);
    }


    //        mk_palm_iov_add_header(iov, mk_cgi_server_addr, mk_api->config->server_addr);
    mk_palm_iov_add_header(iov, mk_cgi_server_name, sr->host);
    mk_palm_iov_add_header(iov, mk_cgi_server_protocol, mk_monkey_protocol);
    mk_palm_iov_add_header(iov, mk_cgi_server_software,
                           mk_api->config->server_software);
    //mk_palm_iov_add_header(iov, mk_cgi_server_signature, sr->host_conf->host_signature);

    if (sr->user_agent.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_user_agent, sr->user_agent);

    if (sr->accept.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_accept, sr->accept);

    if (sr->accept_charset.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_accept_charset,
                               sr->accept_charset);

    if (sr->accept_encoding.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_accept_encoding,
                               sr->accept_encoding);

    if (sr->accept_language.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_accept_language,
                               sr->accept_language);

    if (sr->host.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_host, sr->host);

    if (sr->cookies.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_cookie, sr->cookies);

    if (sr->referer.data)
        mk_palm_iov_add_header(iov, mk_cgi_http_referer, sr->referer);

    //        mk_palm_iov_add_header(iov, mk_cgi_server_port, mk_monkey_port);
    mk_palm_iov_add_header(iov, mk_cgi_gateway_interface, mk_cgi_version);
    //mk_palm_iov_add_header(iov, mk_cgi_remote_addr, cr->ip);
    mk_palm_iov_add_header(iov, mk_cgi_request_uri, sr->uri);
    //mk_palm_iov_add_header(iov, mk_cgi_request_method, sr->method);
    mk_palm_iov_add_header(iov, mk_cgi_script_name, sr->uri);


    /* real path is not an mk_pointer */
    mk_palm_iov_add_header(iov, mk_cgi_script_filename, sr->real_path);
    //mk_palm_iov_add_header(iov, mk_cgi_remote_port, cr->port);
    mk_palm_iov_add_header(iov, mk_cgi_query_string, sr->query_string);
    mk_palm_iov_add_header(iov, mk_cgi_post_vars, sr->post_variables);

    /* CRLF */
    mk_api->iov_add_entry(iov, mk_iov_crlf.data, mk_iov_crlf.len,
                          mk_iov_none, MK_IOV_NOT_FREE_BUF);
    mk_api->iov_add_entry(iov, mk_iov_crlf.data, mk_iov_crlf.len,
                          mk_iov_none, MK_IOV_NOT_FREE_BUF);
    mk_api->iov_add_entry(iov, mk_iov_crlf.data, mk_iov_crlf.len,
                          mk_iov_none, MK_IOV_NOT_FREE_BUF);
    return iov;
}


int mk_palm_send_headers(struct client_request *cr, struct request *sr)
{
    int n;

    sr->headers->status = M_HTTP_OK;
    sr->headers->cgi = SH_CGI;

    /* Chunked transfer encoding */
    if (sr->protocol >= HTTP_PROTOCOL_11) {
        sr->headers->transfer_encoding = MK_HEADER_TE_TYPE_CHUNKED;
    }

    /* Send just headers from buffer */
#ifdef TRACE
    PLUGIN_TRACE("Sending headers to FD %i", cr->socket);
#endif
    n = (int) mk_api->header_send(cr->socket, cr, sr, sr->log);

    /* Monkey core send_headers set TCP_CORK_ON, we need to get
     * back the status to OFF
     */
    mk_api->socket_cork_flag(cr->socket, TCP_CORK_OFF);
#ifdef TRACE
    PLUGIN_TRACE("Send headers returned %i", n);
#endif

    return n;
}


int _mkp_init(void **api, char *confdir)
{
    mk_api = *api;
    palms = 0;

    /* Init some pointers */
    mk_api->pointer_set(&mk_monkey_protocol, HTTP_PROTOCOL_11_STR);
    mk_api->pointer_set(&mk_iov_crlf, MK_IOV_CRLF);
    mk_api->pointer_set(&mk_iov_equal, MK_IOV_EQUAL);

    /* Read configuration */
    mk_palm_conf(confdir);
    return 0;
}

int _mkp_stage_10(struct server_config *config)
{
    mk_cgi_env();
    return 0;
}

int _mkp_stage_40(struct plugin *plugin, struct client_request *cr, struct request *sr)
{
    struct mk_palm *palm;
    struct mk_palm_request *pr;

#ifdef TRACE
    PLUGIN_TRACE("PALM STAGE 40, requesting '%s'", sr->real_path.data);
#endif

    palm = mk_palm_get_handler(&sr->real_path);
    if (!palm) {
#ifdef TRACE
        PLUGIN_TRACE("PALM NOT ME");
#endif

        return MK_PLUGIN_RET_NOT_ME;
    }

    /* Connect to server */
    pr = mk_palm_do_instance(palm, cr, sr);

    if (!pr) {
#ifdef TRACE
        PLUGIN_TRACE("return %i (MK_PLUGIN_RET_END)", MK_PLUGIN_RET_END);
#endif

        return MK_PLUGIN_RET_END;
    }

    /* Register Palm instance */
    mk_palm_request_add(pr);

    /* Register socket with thread Epoll interface */
    mk_api->event_add(pr->palm_fd, MK_EPOLL_READ, plugin, cr, sr);
#ifdef TRACE
    PLUGIN_TRACE("Palm: Event registered for palm_socket=%i", pr->palm_fd);
#endif

    /* Send request */
    mk_palm_send_request(cr, sr);
    mk_palm_send_headers(cr, sr);

#ifdef TRACE
    PLUGIN_TRACE("return %i (MK_PLUGIN_RET_CONTINUE)", MK_PLUGIN_RET_CONTINUE);
#endif

    return MK_PLUGIN_RET_CONTINUE;
}



struct mk_palm_request *mk_palm_do_instance(struct mk_palm *palm,
                                            struct client_request *cr,
                                            struct request *sr)
{
    int ret;
    int palm_socket;

    /* Get Palm handler */
    //   palm = mk_palm_get_handler(&sr->uri);

    /* Connecting to Palm Server */
    palm_socket = mk_api->socket_create();
    ret = mk_api->socket_connect(palm_socket,
                                       palm->server_addr,
                                       palm->server_port);

    if (ret < 0) {
        fprintf(stderr, "\nPalm: Cannot connect to %s on port %i",
                palm->server_addr, palm->server_port);
        return NULL;
    }

    /* Return instance */
    return mk_palm_request_create(cr->socket, palm_socket, cr, sr, palm);
}

void mk_palm_send_request(struct client_request *cr, struct request *sr)
{
    int n;
    ssize_t bytes_iov=-1;
    struct mk_iov *iov;
    struct mk_palm_request *pr;

#ifdef TRACE
    PLUGIN_TRACE("Sending request to Palm Server");
#endif

    pr = mk_palm_request_get_by_http(cr->socket);
    if (pr) {
        if (pr->bytes_sent == 0) {

#ifdef TRACE
            PLUGIN_TRACE("Palm request: '%s'", sr->real_path.data);
#endif
            /* Palm environment vars */
            iov = mk_palm_create_env(cr, sr);

            /* Write request to palm server */
            bytes_iov = (ssize_t )mk_api->iov_send(pr->palm_fd, iov, MK_IOV_SEND_TO_SOCKET);

            if (bytes_iov >= 0){
                pr->bytes_sent += bytes_iov;
                n = (long) bytes_iov;
            }

            /* Socket stuff */
            mk_api->socket_set_nonblocking(pr->palm_fd);
        }
    }

#ifdef TRACE
    PLUGIN_TRACE("Bytes sent to PALM SERVER: %i", pr->bytes_sent);
#endif
}

int mk_palm_send_chunk(int socket, void *buffer, unsigned int len)
{
    int n;
    char *chunk_size=0;
    unsigned long chunk_len=0;


    mk_api->socket_cork_flag(socket, TCP_CORK_ON);
    mk_api->str_build(&chunk_size, &chunk_len, "%x%s", len, MK_CRLF);
    n = mk_api->socket_send(socket, chunk_size, chunk_len);
    mk_api->mem_free(chunk_size);

    if (n < 0) {
#ifdef TRACE
        PLUGIN_TRACE("Error sending chunked header, socket_send() returned %i", n);
#endif
        perror("socket_send");
        return -1;
    }

    n = mk_api->socket_send(socket, buffer, len);
#ifdef TRACE
    PLUGIN_TRACE("SEND CHUNK: requested %i, sent %i", len, n);
#endif

    if (n < 0) {
#ifdef TRACE
        PLUGIN_TRACE("Error sending chunked body, socket_send() returned %i", n);
#endif
        perror("socket_send");
        return -1;
    }

    mk_api->socket_send(socket, MK_CRLF, 2);
    mk_api->socket_cork_flag(socket, TCP_CORK_OFF);
    return n;
}

int mk_palm_send_end_chunk(int socket)
{
    int n;

    n = mk_api->socket_send(socket, "0\r\n\r\n", 5);
    return n;
}

int _mkp_event_read(int sockfd)
{
    int n;
    int ret = -1;
    int headers_end = -1;
    int read_offset = 0;
    struct mk_palm_request *pr;

    pr = mk_palm_request_get(sockfd);

    if (!pr){
#ifdef TRACE
        PLUGIN_TRACE("Invalid palm request, not found");
#endif
        return -1;
    }

    /* Reset read buffer */
    bzero(pr->data_read, MK_PALM_BUFFER_SIZE);

    /* Read data */
    pr->len_read = mk_api->socket_read(pr->palm_fd,
                                       pr->data_read,
                                       (MK_PALM_BUFFER_SIZE - 1));

#ifdef TRACE
    PLUGIN_TRACE("socket read  : %i", pr->len_read);
    PLUGIN_TRACE("headers sent : %i", pr->headers_sent);
#endif

    if (pr->len_read <= 0) {
#ifdef TRACE
        PLUGIN_TRACE("Ending connection: read() = %i", pr->len_read);
#endif
        if (pr->sr->protocol >= HTTP_PROTOCOL_11) {
            n = mk_palm_send_end_chunk(pr->client_fd);
        }

        return MK_PLUGIN_RET_END;
    }
    else if (pr->len_read > 0) {
        if (pr->headers_sent == VAR_OFF) {
            headers_end = (int) mk_api->str_search(pr->data_read,
                                                   MK_IOV_CRLFCRLF);
            /* Look for headers end */
            while (headers_end == -1) {
#ifdef TRACE
                PLUGIN_TRACE("CANNOT FIND HEADERS_END :/");
#endif
                n = mk_api->socket_read(pr->palm_fd,
                                        pr->data_read + pr->len_read,
                                        (MK_PALM_BUFFER_SIZE -1) - pr->len_read);

                if (n > 0) {
                    pr->len_read += n;
                }
                else{
#ifdef TRACE
                    PLUGIN_TRACE("********* FIXME ***********");
#endif
                }

                headers_end = (int) mk_api->str_search(pr->data_read,
                                                       MK_IOV_CRLFCRLF);
            }

            if (headers_end > 0) {
                headers_end += 4;
            }
            else {
#ifdef TRACE
                PLUGIN_TRACE("SOMETHING BAD HAPPENS");
#endif
            }

            /* FIXME: What about if this socket_send wrote partial headers ? ugh! */
            n = mk_api->socket_send(pr->client_fd, pr->data_read, headers_end);

#ifdef TRACE
            PLUGIN_TRACE("Headers sent to HTTP client: %i", n);
#endif

            /* Enable headers flag */
            pr->headers_sent = VAR_ON;
            read_offset = headers_end;
        }

        int sent = 0;
        while (sent != (pr->len_read - read_offset)) {
#ifdef TRACE
            PLUGIN_TRACE("LOOP");
#endif
            if (pr->sr->protocol >= HTTP_PROTOCOL_11) {
                n = mk_palm_send_chunk(pr->client_fd,
                                       pr->data_read + read_offset + sent,
                                       pr->len_read - read_offset - sent);
            }
            else {
                n = mk_api->socket_send(pr->client_fd,
                                        pr->data_read + read_offset + sent,
                                        pr->len_read - read_offset - sent);
            }

            if (n < 0) {
#ifdef TRACE
                PLUGIN_TRACE("WRITE ERROR");
#endif
                perror("socket_send");
                return MK_PLUGIN_RET_END;
            }
            else {
#ifdef TRACE
                PLUGIN_TRACE("BYTES SENT: %i", n);
#endif
                sent += n;
            }
        }

        mk_api->socket_cork_flag(pr->client_fd, TCP_CORK_OFF);
        ret = MK_PLUGIN_RET_CONTINUE;

        mk_palm_request_update(sockfd, pr);
    }
    else {
#ifdef TRACE
        PLUGIN_TRACE("BIG ERROR!");
#endif
    }

    /* Update thread node info */
    mk_palm_request_update(sockfd, pr);

    return ret;
}

int hangup(int sockfd)
{
    struct mk_palm_request *pr;

    pr = mk_palm_request_get(sockfd);
    mk_api->http_request_end(pr->client_fd);
    mk_palm_free_request(sockfd);

#ifdef TRACE
    PLUGIN_TRACE("Hung up socket %i", sockfd);
#endif

    return MK_PLUGIN_RET_END;
}

int _mkp_event_close(int sockfd)
{
#ifdef TRACE
    PLUGIN_TRACE("event close");
#endif

    return hangup(sockfd);
}

int _mkp_event_error(int sockfd)
{
#ifdef TRACE
    PLUGIN_TRACE("event error");
#endif

    return hangup(sockfd);
}

