#include "calserv.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <gnome.h>

void cs_connection_accept(gpointer data, GIOCondition cond,
			  CSServer *server);
static gboolean cs_connection_process(gpointer data, GIOCondition cond,
				      CSConnection *cnx);
static void cs_connection_greet(CSConnection *cnx);

CSServer *
cs_server_new(void)
{
  CSServer *rv;
  struct sockaddr_in addr;

  rv = g_new0(CSServer, 1);

  rv->mainloop = g_main_new(FALSE);
  rv->servfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(rv->servfd < 0) goto errout;

  {
    int n = 1;
    setsockopt(rv->servfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(gnome_config_get_int("/gncal/server/port=7668"));
  addr.sin_addr.s_addr = INADDR_ANY;
  if(bind(rv->servfd, &addr, sizeof(addr))) goto errout;
  if(listen(rv->servfd, 1)) goto errout;

  /* FTSO this GIOChannel crap */
  rv->gioc = g_io_channel_unix_new(rv->servfd);
  g_io_add_watch(rv->gioc, G_IO_IN, (GIOFunc)&cs_connection_accept, rv);
  g_io_channel_ref(rv->gioc);

  return rv;

 errout:
  if(rv->gioc)
    g_io_channel_unref(rv->gioc);
  close(rv->servfd);
  g_free(rv);
  return NULL;
}

void
cs_server_run(CSServer *server)
{
  g_return_if_fail(server);

  g_main_run(server->mainloop);
}

void
cs_server_destroy(CSServer *server)
{
  g_return_if_fail(server);
  
  g_main_quit(server->mainloop);
  g_main_destroy(server->mainloop);

  close(server->servfd);
  g_io_channel_unref(server->gioc);

  g_list_foreach(server->connections, (GFunc)cs_connection_unref, NULL);

  g_free(server);
}

static gboolean
cs_connection_process(gpointer data, GIOCondition cond,
		      CSConnection *cnx)
{
  char readbuf[257];
  int rsize;
  gboolean cont;

  if(cond & (G_IO_HUP|G_IO_NVAL|G_IO_ERR)) {
    cs_connection_unref(cnx); /* give up server list ref */
    return FALSE;
  }

  g_return_val_if_fail(cond & G_IO_IN, FALSE);

  /* read the data */
  rsize = read(cnx->fd, readbuf, sizeof(readbuf) - 1);
  if(!rsize) {
    cs_connection_unref(cnx); /* give up server list ref */
    return FALSE;
  }
  readbuf[rsize] = '\0';
  g_string_append(cnx->parse.rdbuf, readbuf);

  if (rsize == 0){
    cs_connection_unref (cnx); /* give up server list ref */
    return FALSE;
  }

  cs_connection_ref(cnx); /* attain in-parse ref */

  do {
      gboolean error = FALSE;

      if (try_to_parse (&cnx->parse, rsize, &error, &cont)){
        cs_connection_process_command(cnx);
        cs_cmdarg_destroy(cnx->parse.curcmd.args);
	cnx->parse.curcmd.args = NULL;
	cnx->parse.setptr = &cnx->parse.curcmd.args;

        g_free(cnx->parse.curcmd.id);
        g_free(cnx->parse.curcmd.name);
        cnx->parse.rs = RS_ID; /* Next? */
      }

      if (error) {
        cs_connection_unref(cnx); /* give up in-parse ref */
        cs_connection_unref(cnx); /* give up server list ref */
	return FALSE;
      }
  } while (cont);

  cs_connection_unref(cnx);

  return TRUE;
}

static void
cs_connection_greet(CSConnection *cnx)
{
#define CS_greeting "* OK "CS_capabilities" It is actually quite an aweful day today.\n"

  write(cnx->fd, CS_greeting, sizeof(CS_greeting)-1);
}

void cs_connection_accept(gpointer data, GIOCondition cond,
			  CSServer *server)
{
  CSConnection *cnx;
  int fd, itmp;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  itmp = sizeof(addr);
  fd = accept(server->servfd, &addr, &itmp);
  if(fd < 0) return;

  cnx = g_new0(CSConnection, 1);
  cnx->parse.rdbuf = g_string_new(NULL);
  cnx->parse.setptr = &cnx->parse.curcmd.args;
  cnx->serv = server;
  cnx->fd = fd;
  fcntl(cnx->fd, F_GETFL, &itmp);
  itmp |= O_NONBLOCK;
  fcntl(cnx->fd, F_SETFL, &itmp);  

  cnx->fh = fdopen(fd, "a+");
  setvbuf(cnx->fh, cnx->wrbuf, _IOLBF, sizeof(cnx->wrbuf));
  g_assert(cnx->fh);

  cnx->gioc = g_io_channel_unix_new(fd);
  g_io_channel_ref(cnx->gioc);
  g_io_add_watch(cnx->gioc, G_IO_IN|G_IO_HUP|G_IO_NVAL,
		 (GIOFunc)&cs_connection_process, cnx);

  server->connections = g_list_prepend(server->connections, cnx);
  cs_connection_ref(cnx); /* attain server list ref */
  cs_connection_greet(cnx);
}

void
cs_connection_ref(CSConnection *cnx)
{
  cnx->refcount++;
}

void
cs_connection_unref(CSConnection *cnx)
{
  CSServer *server;

  cnx->refcount--;
  if(cnx->refcount > 0)
    return;

  server = cnx->serv;
  server->connections = g_list_remove(server->connections, cnx);
  fclose(cnx->fh); /* also closes fd */
  g_io_channel_unref(cnx->gioc);
  g_string_free(cnx->parse.rdbuf, TRUE);
  g_free(cnx->authid);
  if(cnx->active_cal)
    backend_close_calendar(cnx->active_cal);

  g_free(cnx);
}     
