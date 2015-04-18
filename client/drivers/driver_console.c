/* driver_console.c
 * By Ron Bowes
 *
 * See LICENSE.md
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include "libs/buffer.h"
#include "libs/log.h"
#include "libs/memory.h"
#include "libs/select_group.h"
#include "libs/types.h"

#include "driver_console.h"

/* There can only be one driver_console, so store these as global variables. */
static SELECT_RESPONSE_t console_stdin_recv(void *group, int socket, uint8_t *data, size_t length, char *addr, uint16_t port, void *d)
{
  driver_console_t *driver = (driver_console_t*) d;

  /* TODO: Tell the controller that we have data */
  buffer_add_bytes(driver->outgoing_data, data, length);

  return SELECT_OK;
}

static SELECT_RESPONSE_t console_stdin_closed(void *group, int socket, void *d)
{
  /* When the stdin pipe is closed, the stdin driver signals the end. */
  driver_console_t *driver = (driver_console_t*) d;

  /* Record that we've been shut down - we'll continue reading to the end of the buffer, still. */
  driver->is_shutdown = TRUE;

  return SELECT_CLOSE_REMOVE;
}

void driver_console_data_received(driver_console_t *driver, uint8_t *data, size_t length)
{
  size_t i;

  for(i = 0; i < length; i++)
    fputc(data[i], stdout);
}

uint8_t *driver_console_get_outgoing(driver_console_t *driver, size_t *length, size_t max_length)
{
  /* If the driver has been killed and we have no bytes left, return NULL to close the session. */
  if(driver->is_shutdown && buffer_get_remaining_bytes(driver->outgoing_data) == 0)
    return NULL;

  return buffer_read_remaining_bytes(driver->outgoing_data, length, max_length, TRUE);
}

driver_console_t *driver_console_create(select_group_t *group)
/*, char *name, char *download, int first_chunk)*/
{
  driver_console_t *driver = (driver_console_t*) safe_malloc(sizeof(driver_console_t));

  driver->group         = group;
  driver->is_shutdown   = FALSE;
  driver->outgoing_data = buffer_create(BO_LITTLE_ENDIAN);

#ifdef WIN32
  /* On Windows, the stdin_handle is quite complicated, and involves a sub-thread. */
  HANDLE stdin_handle = get_stdin_handle();
  select_group_add_pipe(group, -1, stdin_handle, driver);
  select_set_recv(group,       -1, console_stdin_recv);
  select_set_closed(group,     -1, console_stdin_closed);
#else
  /* On Linux, the stdin_handle is easy. */
  int stdin_handle = STDIN_FILENO;
  select_group_add_socket(group, stdin_handle, SOCKET_TYPE_STREAM, driver);
  select_set_recv(group,         stdin_handle, console_stdin_recv);
  select_set_closed(group,       stdin_handle, console_stdin_closed);
#endif

#if 0
  message_options_t options[4];
  driver->name        = name ? name : "[unnamed console]";
  driver->download    = download;
  driver->first_chunk = first_chunk;

  options[0].name    = "name";
  options[0].value.s = driver->name;

  if(driver->download)
  {
    options[1].name    = "download";
    options[1].value.s = driver->download;

    options[2].name    = "first_chunk";
    options[2].value.i = driver->first_chunk;
  }
  else
  {
    options[1].name = NULL;
  }

  options[3].name    = NULL;
#endif

  return driver;
}

void driver_console_destroy(driver_console_t *driver)
{
#if 0
  if(driver->name)
    safe_free(driver->name);
  if(driver->download)
    safe_free(driver->download);
#endif
  safe_free(driver);
}

void driver_console_close(driver_console_t *driver)
{
  printf("TODO: Close console driver in some meaningful way\n");
}