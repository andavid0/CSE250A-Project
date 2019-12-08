#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ipv6/uip-debug.h"

#include <stdio.h>

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_PORT 61618
#define UDP_PORT_REC 1234

#define SEND_INTERVAL		(4 * CLOCK_SECOND)

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define SEND_INTERVAL_RPL     (10 * CLOCK_SECOND)

static struct simple_udp_connection broadcast_connection;
static struct simple_udp_connection unicast_connection;
static struct simple_udp_connection udp_conn;

#ifndef SIZE
#define SIZE 100
#endif

/*---------------------------------------------------------------------------*/
PROCESS(udp_process, "UDP broadcast process");
PROCESS(receive_data, "get sensor node data process");
PROCESS(routing_process, "route to sink");
AUTOSTART_PROCESSES(&udp_process);

/*---------------------------------------------------------------------------*/

int k = 0;


static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);
}

static void
receiver_SN(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  printf("Data received from ");
  uip_debug_ipaddr_print(sender_addr);
  printf(" on port %d from port %d with length %d: '%s'\n",
         receiver_port, sender_port, datalen, data);
}

static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{

  LOG_INFO("Received response '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
#if LLSEC802154_CONF_ENABLED
  LOG_INFO_(" LLSEC LV:%d", uipbuf_get_attr(UIPBUF_ATTR_LLSEC_LEVEL));
#endif
  LOG_INFO_("\n");

}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer stop_timer;
  //static struct etimer send_timer;
  uip_ipaddr_t addr;

  PROCESS_BEGIN();

  simple_udp_register(&broadcast_connection, UDP_PORT,
                      NULL, UDP_PORT,
                      receiver);

  etimer_set(&periodic_timer, 20 * CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
  etimer_set(&periodic_timer, SEND_INTERVAL);
  etimer_set(&stop_timer, 10 * CLOCK_SECOND);
  
  while (1){
    while(!etimer_expired(&stop_timer)) {
      uint8_t buf[SIZE];
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);

      printf("Sending broadcast\n");
      uip_create_linklocal_allnodes_mcast(&addr);
      simple_udp_sendto(&broadcast_connection, buf, sizeof(buf), &addr);
    }
    printf("DONE\n");
    process_start(&receive_data, NULL);
    PROCESS_EXIT();
  }
  

  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(receive_data, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  process_start(&routing_process, NULL);

  simple_udp_register(&unicast_connection, UDP_PORT_REC,
                      NULL, UDP_PORT_REC, receiver_SN);

  etimer_set(&et, CLOCK_SECOND);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    etimer_reset(&et);
  }

  
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(routing_process, ev, data)
{ 

  static struct etimer et;
  static struct etimer periodic_timer;
  static unsigned count;
  static char str[32];
  uip_ipaddr_t dest_ipaddr;


  PROCESS_BEGIN();

  etimer_set(&et, 2*CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, SEND_INTERVAL_RPL);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      /* Send to DAG root */
      LOG_INFO("Sending request %u to ", count);
      LOG_INFO_6ADDR(&dest_ipaddr);
      LOG_INFO_("\n");
      snprintf(str, sizeof(str), "hello %d", count);
      simple_udp_sendto(&udp_conn, str, strlen(str), &dest_ipaddr);
      count++;
    } else {
      LOG_INFO("Not reachable yet\n");
    }

    /* Add some jitter */
    etimer_set(&periodic_timer, SEND_INTERVAL_RPL
      - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
  }



  PROCESS_END();


}