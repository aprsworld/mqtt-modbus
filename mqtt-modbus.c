#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <mosquitto.h>
#include <modbus.h>

#define mqtt_host "localhost"
#define mqtt_port 1883

static int run = 1;
modbus_t *mb;


void handle_signal(int s)
{
	run = 0;
}

void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	printf("connect callback, rc=%d\n", result);
}

void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message) {
	char data[4];
	bool match = 0;
	uint16_t value = 0;

	printf("got message '%.*s' for topic '%s'\n", message->payloadlen, (char*) message->payload, message->topic);

	/* local copy of our message */
	strncpy(data, (char *) message->payload,3);
	data[3]='\0';

	

	/* light above big door */
	mosquitto_topic_matches_sub("refarm/shop/exteriorLightSouth", message->topic, &match);
	if (match) {
		printf("got message for exterior light south\n");

		/* connect to modbus gateway */
		mb = modbus_new_tcp("192.168.32.228", 502);

//		modbus_set_debug(mb,TRUE);

		if ( NULL == mb ) {
			fprintf(stderr,"# error creating modbus instance: %s\n", modbus_strerror(errno));
			return;

		}

		if ( -1 == modbus_connect(mb) ) {
			fprintf(stderr,"# modbus connection failed: %s\n", modbus_strerror(errno));
			modbus_free(mb);
			return;
		}

		/* set slave address of device we want to talk to */
		if ( 0 != modbus_set_slave(mb,40) ) {
			fprintf(stderr,"# modbus_set_slave() failed: %s\n", modbus_strerror(errno));
			modbus_free(mb);
			return;

		}

		/* "ON" (case insensitive) sends a value 1, everything else sends value 0 */
		if ( NULL != strcasestr(data,"ON") ) {
			value=1;
		} 


		if ( -1 == modbus_write_register(mb,0,value) ) {
			fprintf(stderr,"# modbus_write_register() failed: %s\n", modbus_strerror(errno));
		} else {
			fprintf(stderr,"# wrote %d to register 0\n",value);
		}

		modbus_close(mb);
		modbus_free(mb);

	}

	

}

int main(int argc, char **argv) {
	uint8_t reconnect = true;
	char clientid[24];
	struct mosquitto *mosq;
	int rc = 0;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	mosquitto_lib_init();

	memset(clientid, 0, 24);
	snprintf(clientid, 23, "mysql_log_%d", getpid());
	mosq = mosquitto_new(clientid, true, 0);

	if (mosq) {
		mosquitto_connect_callback_set(mosq, connect_callback);
		mosquitto_message_callback_set(mosq, message_callback);

		rc = mosquitto_connect(mosq, mqtt_host, mqtt_port, 60);

		mosquitto_subscribe(mosq, NULL, "refarm/shop/#", 0);

		while (run) {
			rc = mosquitto_loop(mosq, -1, 1);

			if ( run && rc ) {
				printf("connection error!\n");
				sleep(10);
				mosquitto_reconnect(mosq);
			}
		}
		mosquitto_destroy(mosq);
	}

	mosquitto_lib_cleanup();

	return rc;
}

