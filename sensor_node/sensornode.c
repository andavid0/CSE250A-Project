#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/link-stats.h"
#include "net/packetbuf.h"
#include "net/ipv6/uip-debug.h"
#include "sys/energest.h"


#include <stdio.h>
#include <stdlib.h>

#define UDP_PORT 61618
#define UDP_PORT_SEND 1234

#define SEND_INTERVAL   (10 * CLOCK_SECOND)

#define CH_num 2

#define ON_PERIOD_TEMP  4
#define OFF_PERIOD_TEMP (15 - ON_PERIOD_TEMP)

#define ON_PERIOD_WIND 6
#define OFF_PERIOD_WIND (15 - ON_PERIOD_WIND)

static struct simple_udp_connection broadcast_connection;
static struct simple_udp_connection unicast_connection;


#ifndef SIZE
#define SIZE 100
#endif


/*---------------------------------------------------------------------------*/
PROCESS(find_CH_process, "Clusterhead finding process");
PROCESS(send_to_CH, "Sending data to clusterhead process");
PROCESS(components, "Duty cycle of components");
PROCESS(energy_monitor, "Energy monitoring");

AUTOSTART_PROCESSES(&find_CH_process, &energy_monitor);
/*---------------------------------------------------------------------------*/

/* Define a buffer to copy RSSI values received from CH broadcasts */
int16_t rssi_buffer[CH_num];

/* Define variables to copy CH IP addresses received from broadcasts */
uip_ipaddr_t first_ip_received;
uip_ipaddr_t second_ip_received;
uip_ipaddr_t CH_IP_addr;
unsigned long sim_start; 
unsigned long sim_end; 
unsigned long sim_time; 

int k = -1;
static int flag = 0;
static int no_power = 0;


static inline unsigned long
to_seconds(uint64_t time)
{
  return (unsigned long)(time / ENERGEST_SECOND);
}


static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  static int msg;

  if (k >= CH_num-1){
    flag = 1;
    return;
  }
  printf("k is %d\n",k);
  // increment message number
  msg++;
  k++;
  printf("Data %d received length %d\n",
         msg, datalen);
  printf("Data received from ");
  uiplib_ipaddr_print(sender_addr);
  printf("\n");

  /* Obtain the RSSI value from the incoming broadcast */
  int16_t packet_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if (k == 0){
    uip_ipaddr_copy(&first_ip_received, sender_addr);
    printf("Data received from (copy) ");
    uiplib_ipaddr_print(&first_ip_received);
    printf("\n");
  }
  if (k == 1){
    uip_ipaddr_copy(&second_ip_received, sender_addr);
    printf("Data received from (copy) ");
    uiplib_ipaddr_print(&second_ip_received);
    printf("\n");
  }

  rssi_buffer[k] = packet_rssi;
  printf("strength: %d \n", rssi_buffer[k]);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(find_CH_process, ev, data)
{
  static struct etimer periodic_timer;
  //static struct etimer send_timer;
  uip_ipaddr_t addr;
  //static int alive;
  const uip_ipaddr_t *default_prefix;

  PROCESS_BEGIN();

  sim_start = clock_seconds();
  printf("Start time: %lu\n", sim_start);

  default_prefix = uip_ds6_default_prefix();
  uip_ip6addr_copy(&addr, default_prefix);
  addr.u16[7] = UIP_HTONS(2);
  uip_ds6_addr_add(&addr, 0, ADDR_AUTOCONF);

  simple_udp_register(&broadcast_connection, UDP_PORT,
                      NULL, UDP_PORT,
                      receiver);

  etimer_set(&periodic_timer, 20 * CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

  while(1) {

    if (flag == 1){
      if (CH_num > 1){
        if (abs(rssi_buffer[0]) < abs(rssi_buffer[1])){
          printf("Clusterhead IP is: ");
          uiplib_ipaddr_print(&first_ip_received);
          printf("\n");
          uip_ipaddr_copy(&CH_IP_addr, &first_ip_received);
        }else if (abs(rssi_buffer[0]) > abs(rssi_buffer[1])) {
          printf("Clusterhead IP is: ");
          uiplib_ipaddr_print(&second_ip_received);
          printf("\n");
          uip_ipaddr_copy(&CH_IP_addr, &second_ip_received);

        }
        
      }else {
        printf("IP address is: ");
        uiplib_ipaddr_print(&first_ip_received);
        printf("\n");
        uip_ipaddr_copy(&CH_IP_addr, &first_ip_received);
      }
      
      process_start(&send_to_CH, NULL);
      process_start(&components, NULL);
      PROCESS_EXIT();

    }
 
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(send_to_CH, ev, data)
{

  static struct etimer periodic_timer;
  //static struct etimer send_timer;
  static int message_number = 0;
  char buf[20];

  PROCESS_BEGIN();


  printf("HELLO I STARTED\n");

  //printf("\nCH address is: ");
  //uiplib_ipaddr_print(data);
  //printf("\n");


  simple_udp_register(&unicast_connection, UDP_PORT_SEND,
                      NULL, UDP_PORT_SEND, receiver);

  etimer_set(&periodic_timer, 20*CLOCK_SECOND);
  
  while(1) {

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);

    printf("Sending unicast to ");
    //uiplib_ipaddr_print(&CH_IP_addr);
    //printf("\n");
    sprintf(buf, "Message %d", message_number);
    message_number++;
    simple_udp_sendto(&unicast_connection, buf, strlen(buf) + 1, &CH_IP_addr);

    if (no_power == 1){
      printf("No power here either!\n");
      PROCESS_EXIT();
    }
    
  }


  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(components, ev, data)
{

  //static struct etimer wait_timer;
  static struct etimer ON_TIMER_TEMP;
  static struct etimer OFF_TIMER_TEMP;
  static struct etimer ON_TIMER_WIND;
  static struct etimer OFF_TIMER_WIND;

  PROCESS_BEGIN();
  //energest_init();

  printf("I began too!!\n");

  /* Set timers */
  etimer_set(&ON_TIMER_TEMP, CLOCK_SECOND * ON_PERIOD_TEMP);
  etimer_set(&ON_TIMER_WIND, CLOCK_SECOND * ON_PERIOD_WIND);

  /* Turn on sensors */
  ENERGEST_ON(ENERGEST_TYPE_TEMP);
  ENERGEST_ON(ENERGEST_TYPE_WIND);

  
  while(1) {

    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && (data == &ON_TIMER_TEMP || data == &OFF_TIMER_TEMP || data == &ON_TIMER_WIND || data == &OFF_TIMER_WIND));

    if (data == &ON_TIMER_TEMP){
      ENERGEST_OFF(ENERGEST_TYPE_TEMP);
      etimer_set(&OFF_TIMER_TEMP, CLOCK_SECOND * OFF_PERIOD_TEMP);
      energest_flush();
      //printf("\nTEMP turning OFF, been on %4lus\n", to_seconds(energest_type_time(ENERGEST_TYPE_TEMP)));
      //printf("current time is: %4lus \n", to_seconds(ENERGEST_CURRENT_TIME()));
      //printf("total time is: %4lus \n", to_seconds(ENERGEST_GET_TOTAL_TIME()));
      
    }

    if (data == &OFF_TIMER_TEMP){
      ENERGEST_ON(ENERGEST_TYPE_TEMP);
      etimer_set(&ON_TIMER_TEMP, CLOCK_SECOND * ON_PERIOD_TEMP);
      //energest_flush();
      //printf("\nTEMP turning ON, been on %4lus\n", to_seconds(energest_type_time(ENERGEST_TYPE_TEMP)));
      //printf("current time is: %4lus \n", to_seconds(ENERGEST_CURRENT_TIME()));
      //printf("total time is: %4lus \n", to_seconds(ENERGEST_GET_TOTAL_TIME()));

    }   

    if (data == &ON_TIMER_WIND){
      ENERGEST_OFF(ENERGEST_TYPE_WIND);
      etimer_set(&OFF_TIMER_WIND, CLOCK_SECOND * OFF_PERIOD_WIND);
      //energest_flush();
      //printf("\nWIND turning OFF, been on %4lus\n", to_seconds(energest_type_time(ENERGEST_TYPE_WIND)));
      //printf("current time is: %4lus \n", to_seconds(ENERGEST_CURRENT_TIME()));
      //printf("total time is: %4lus \n", to_seconds(ENERGEST_GET_TOTAL_TIME()));

    }

    if (data == &OFF_TIMER_WIND){
      ENERGEST_ON(ENERGEST_TYPE_WIND);
      etimer_set(&ON_TIMER_WIND, CLOCK_SECOND * ON_PERIOD_WIND);
      //energest_flush();
      //printf("\nWIND turning ON, been on %4lus\n", to_seconds(energest_type_time(ENERGEST_TYPE_WIND)));
      //printf("current time is: %4lus \n", to_seconds(ENERGEST_CURRENT_TIME()));
      //printf("total time is: %4lus \n", to_seconds(ENERGEST_GET_TOTAL_TIME()));

    } 
                 
  }
    
  PROCESS_END();
  
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(energy_monitor, ev, data)
{

  static struct etimer periodic_timer;
  unsigned long on_time_temp;
  unsigned long energy_temp;
  unsigned long energy_wind;
  unsigned long on_time_wind;
  unsigned long energy_radio;
  unsigned long on_time_radio;

  //unsigned long battery = 1000000000;
  unsigned long battery = 10000;
  unsigned long total_energy;



  PROCESS_BEGIN();
  printf("HERE\n");

  etimer_set(&periodic_timer, CLOCK_SECOND * 1);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_reset(&periodic_timer);

    energest_flush();
    on_time_temp = to_seconds(energest_type_time(ENERGEST_TYPE_TEMP));
    on_time_wind = to_seconds(energest_type_time(ENERGEST_TYPE_WIND));
    on_time_radio = to_seconds(energest_type_time(ENERGEST_TYPE_LISTEN));
    energy_temp = 5*on_time_temp;
    energy_wind = 8*on_time_wind;
    energy_radio = 8*on_time_radio;


    total_energy = (battery - (energy_temp + energy_wind + energy_radio));

    printf("Total energy is: %lu \n", total_energy);

    if (total_energy > 400000000){
      no_power = 1;
      sim_end = clock_seconds();
      sim_time = sim_end - sim_start;
      printf("\n\n Sim time: %lu\n\n", sim_time);
      printf("\n\n No power!!\n\n");
      PROCESS_EXIT();
    }


  }

  PROCESS_END();

}