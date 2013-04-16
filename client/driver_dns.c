/* driver_dns.c
 * Created March/2013
 * By Ron Bowes
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "buffer.h"
#include "dns.h"
#include "memory.h"
#include "select_group.h"
#include "udp.h"

#include "driver_dns.h"

dns_driver_t *dns_driver_create(char *domain, char *dns_host, uint16_t dns_port, select_group_t *group)
{
  dns_driver_t *dns_driver = (dns_driver_t*)safe_malloc(sizeof(dns_driver_t));
  dns_driver->s            = -1;
  dns_driver->domain       = safe_strdup(domain);
  dns_driver->dns_host     = safe_strdup(dns_host);
  dns_driver->dns_port     = dns_port;
  dns_driver->group        = group;

  /* TODO: This should be a list or something, not a buffer */
  dns_driver->incoming_data = buffer_create(BO_BIG_ENDIAN);

  return dns_driver;
}

static SELECT_RESPONSE_t recv_callback(void *group, int s, uint8_t *data, size_t length, char *addr, uint16_t port, void *param)
{
  dns_driver_t *driver = param;
  dns_t        *dns    = dns_create_from_packet(data, length);

  /* TODO */
  if(dns->rcode != DNS_RCODE_SUCCESS)
  {
    /* TODO: Handle errors more gracefully */
    switch(dns->rcode)
    {
      case DNS_RCODE_FORMAT_ERROR:
        printf("DNS ERROR: RCODE_FORMAT_ERROR\n");
        break;
      case DNS_RCODE_SERVER_FAILURE:
        printf("DNS ERROR: RCODE_SERVER_FAILURE\n");
        break;
      case DNS_RCODE_NAME_ERROR:
        printf("DNS ERROR: RCODE_NAME_ERROR\n");
        break;
      case DNS_RCODE_NOT_IMPLEMENTED:
        printf("DNS ERROR: RCODE_NOT_IMPLEMENTED\n");
        break;
      case DNS_RCODE_REFUSED:
        printf("DNS ERROR: RCODE_REFUSED\n");
        break;
      default:
        printf("DNS ERROR: Unknown error code (0x%04x)\n", dns->rcode);
        break;
    }
  }
  else if(dns->question_count != 1)
  {
    printf("DNS returned the wrong number of response fields.\n");
    exit(1);
  }
  else if(dns->answer_count != 1)
  {
    printf("DNS returned the wrong number of response fields.\n");
    exit(1);
  }
  else if(dns->answers[0].type == DNS_TYPE_TEXT)
  {
    char *answer;
    char buf[3];
    size_t i;

    answer = dns->answers[0].answer->TEXT.text;

    printf("Received: %s\n", answer);
    if(!strcmp(answer, driver->domain))
    {
      printf("WARNING: Received a 'nil' answer; ignoring\n");
    }
    else
    {
      /* Find the domain, which should be at the end of the string */
      char *domain = strstr(answer, driver->domain);
      if(!domain)
      {
        printf("ERROR: Answer didn't contain the domain\n");
      }
      else
      {
        /* Loop through the part of the answer before the 'domain' */
        for(i = 0; answer + i < domain; i += 2)
        {
          /* Validate the answer */
          if(answer[i] == '.')
          {
            /* ignore */
          }
          else if(answer[i+1] == '.')
          {
            printf("WARNING: Answer contained an odd number of digits\n");
          }
          else if(!isxdigit((int)answer[i]))
          {
            printf("WARNING: Answer contained an invalid digit: '%c'\n", answer[i]);
          }
          else if(!isxdigit((int)answer[i+1]))
          {
            printf("WARNING: Answer contained an invalid digit: '%c'\n", answer[i+1]);
          }
          else
          {
            buf[0] = answer[i];
            buf[1] = answer[i + 1];
            buf[2] = '\0';

            buffer_add_int8(driver->incoming_data, strtol(buf, NULL, 16));
          }
        }
      }
    }
  }
  else
  {
    printf("Unknown DNS type returned\n");
    exit(1);
  }

  dns_destroy(dns);

  return SELECT_OK;
}

void driver_dns_send(void *driver, uint8_t *data, size_t length)
{
  size_t        i;
  dns_driver_t *d = (dns_driver_t*) driver;
  dns_t        *dns;
  buffer_t     *buffer;
  uint8_t      *encoded_bytes;
  size_t        encoded_length;
  uint8_t      *dns_bytes;
  uint32_t      dns_length;

  if(d->s == -1)
  {
    d->s = udp_create_socket(0, "0.0.0.0");

    if(d->s == -1)
    {
      printf("[[DNS]] :: couldn't create socket!\n");
      return;
    }

    /* If it succeeds, add it to the select_group */
    select_group_add_socket(d->group, d->s, SOCKET_TYPE_STREAM, d);
    select_set_recv(d->group, d->s, recv_callback);
  }

  assert(d->s != -1); /* Make sure we have a valid socket. */
  assert(data); /* Make sure they aren't trying to send NULL. */
  assert(length > 0); /* Make sure they aren't trying to send 0 bytes. */

  buffer = buffer_create(BO_BIG_ENDIAN);
  for(i = 0; i < length; i++)
  {
    char hex_buf[3];
    sprintf(hex_buf, "%02x", data[i]);
    buffer_add_bytes(buffer, hex_buf, 2);
  }
  buffer_add_ntstring(buffer, ".skullseclabs.org");
  encoded_bytes = buffer_create_string_and_destroy(buffer, &encoded_length);

  dns = dns_create(rand() % 0xFFFF, DNS_OPCODE_QUERY, DNS_FLAG_RD, DNS_RCODE_SUCCESS);
  dns_add_question(dns, encoded_bytes, DNS_TYPE_TEXT, DNS_CLASS_IN);
  dns_bytes = dns_to_packet(dns, &dns_length);

  udp_send(d->s, d->dns_host, d->dns_port, dns_bytes, dns_length);

  safe_free(dns_bytes);
  safe_free(encoded_bytes);
}

uint8_t *driver_dns_recv(void *driver, size_t *length, size_t max_length)
{
  uint8_t *ret;

  dns_driver_t *d = (dns_driver_t*) driver;

  if(buffer_get_remaining_bytes(d->incoming_data) > 0)
  {
    /* Read the rest of the buffer. */
    ret = buffer_read_remaining_bytes(d->incoming_data, length, -1, FALSE);

    /* Consume the bytes from the buffer */
    buffer_consume(d->incoming_data, *length);

    return ret;
  }

  /* By default, return NULL */
  *length = 0;
  return NULL;
}

void driver_dns_close(void *driver)
{
  dns_driver_t *d = (dns_driver_t*) driver;

  printf("[[UDP]] :: close()\n");

  assert(d->s && d->s != -1); /* We can't close a closed socket */

  /* Remove from the select_group */
  select_group_remove_and_close_socket(d->group, d->s);
  d->s = -1;
}

void driver_dns_cleanup(void *driver)
{
  dns_driver_t *d = (dns_driver_t*) driver;

  printf("[[DNS]] :: cleanup()\n");

  /* Ensure the driver is closed */
  if(d->s != -1)
    driver_dns_close(driver);

  buffer_destroy(d->incoming_data);

  safe_free(d->domain);
  safe_free(d->dns_host);
  safe_free(d);
}