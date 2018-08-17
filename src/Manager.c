/*
 ============================================================================
 Name        : LennyIOTInterface.c
 Author      : LennyB
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */
/*-----------------------------------------------------------------------------
 --|
 --| Includes
 --|
 -----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h> // One of our functions is variadic
#include <pthread.h> // One of our calls is not thread-safe


#include "Manager.h"
#include "Utilities.h"

// Include for internal MQTT dubbed "lan MQTT" below
// (to smart home devices, just the garage sensor at the moment)
#include "MQTTClient.h"

// Includes for External MQTT (i.e. link to AWS IOT) dubbed "aws MQTT" below
#include "aws_iot_config.h"
#include "aws_iot_error.h"
#include "aws_iot_log.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

// Smart home devices
#include "VirtualGarage/GarageShadow.h"

/*-----------------------------------------------------------------------------
 --|
 --| Defines
 --|
 -----------------------------------------------------------------------------*/
// General defines for MQTT
#define ADDRESS     "tcp://localhost:1883"
#define CLIENTID    "LocalMQTTInterface"
#define QOS         1
#define TIMEOUT     10000L

/*-----------------------------------------------------------------------------
 --|
 --| Private Data
 --|
 -----------------------------------------------------------------------------*/

static AWS_IoT_Client AWSMQTTclient;
static 	MQTTClient LANMQTTclient;

// System-level info for AWS Goes here. Currently just garage health.
static bool garageIsDead = false;
static const jsonStruct_t garageSensorHealth = {
 		 "garageSensorDead",
 		 &garageIsDead,
 		 sizeof(unsigned),
 		 SHADOW_JSON_BOOL,
 		 NULL,
  };

pthread_mutex_t lock;
/*
 * Note from AWS IOT SDK
 * The delta message is always sent on the "state" key in the json
 * Any time messages are bigger than AWS_IOT_MQTT_RX_BUF_LEN the underlying MQTT
 * library will ignore it.
 * The maximum size of the message that can be received is
 * limited to the AWS_IOT_MQTT_RX_BUF_LEN
 */
char stringToEchoDelta[SHADOW_MAX_SIZE_OF_RX_BUFFER];
/*------------------------------------------------------------------------------
 --|
 --| Private Function Prototypes
 --|
 -----------------------------------------------------------------------------*/

static int lanMQTTInit();

/*------------------------------------------------------------------------------
 --|
 --| Private Function Bodies
 --|
 -----------------------------------------------------------------------------*/


// LAN-MQTT message delivered. We don't currently care. TODO
static void lanMQTTMsgDelivered(void *context, MQTTClient_deliveryToken dt) {
	printf("Garage Got It\n");
	return;
}
// LAN-MQTT message received
static int lanMQTTNewMsgReceived(void *context, char *topicName, int topicLen,
		MQTTClient_message *message) {

	// MQTT API dictates we return the  following
	const bool MSG_NOT_HANDLED = 0;
	//const bool MSG_HANDLED = 1;

	bool retVal = MSG_NOT_HANDLED;
	retVal |= GarageHandleDataFromHW(topicName, message->payload);

	// Turns out theres a memory leak without these!
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);

	// Other smarthome modules would go hereret ex:
	// retVal |= HandleDishWasherData(topicName, message->payload);
	return (int)retVal;
}

// TODO quick qnd dirty handling currently for this scenario. We will also,
// detect the loss of connectivity indirectly (via module health check) and
// report it to AWS IOT.
static void lanMQTTConnLost(void *context, char *cause) {
	printf("Reconnecting...\n");
	// Quick and dirty. Indefinately attemp to reconnect to MQTT server
	while (MQTTCLIENT_SUCCESS != lanMQTTInit())
		sleep(2);

return;
}

// LAN-MQTT basic initialization
// Establishes connection to the internal MQTT network. This Raspberry PI is
// the MQTT broker. It connects to localhost and authenticates. This connection
// is not encrypted.
static int lanMQTTInit(	) {

	MQTTClient_create(&LANMQTTclient, ADDRESS, CLIENTID,
	MQTTCLIENT_PERSISTENCE_NONE, NULL);

	// Set up connection options
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;
	// Client needs authentication to connect to LAN-MQTT server his is *not*
	// super secure, but we are not overly concerned with LAN security
	conn_opts.username = "ESP8266_1";
	conn_opts.password = "mqtt_pw1";
	// Register callbacks for the things we care about: When the connection was
	// lost, when a new message arrived and when our message was delivered
	MQTTClient_setCallbacks(LANMQTTclient, NULL, lanMQTTConnLost,
			lanMQTTNewMsgReceived, lanMQTTMsgDelivered);

	// Now let's try to connect to mosquitto
	int rct;
	if ((rct = MQTTClient_connect(LANMQTTclient, &conn_opts))
			!= MQTTCLIENT_SUCCESS) {
		printf("Failed to connect, return code %d\n", rct);
		return(rct);
	}

	// Setup the manager to listen to topics our modules (just garage) publish
	stringList_structType *garagePubList;
	garagePubList = GarageGetTopics(MODULE_PUBLISHES);
	// Now do the dirty work of subscribing to these topics
	for (int i = 0; i < garagePubList->numTopics; i++)
	{
		MQTTClient_subscribe(LANMQTTclient, garagePubList->topicList[i], QOS);
	}
	GarageGetTopics_free(garagePubList);


	return rct;
}

// Called when shadow delta appears
static void awsDeltaCallback(const char *pJsonValueBuffer, uint32_t valueLength,
		jsonStruct_t *pJsonStruct_t)
{
	GarageHandleDataFromAWS(pJsonValueBuffer);
return;
}

// Establishes connection to the AWS IOT cloud. Manages TLS handshaking.
// Sets up some parameters
static int awsMQTTInit()
{
	IoT_Error_t rc = FAILURE;

	// Security-related path setup
	static char certDirectory[PATH_MAX + 1] = "certs";
	char rootCA[PATH_MAX + 1];
	char clientCRT[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	getcwd(CurrentWD, sizeof(CurrentWD));
	snprintf(rootCA, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory,
			AWS_IOT_ROOT_CA_FILENAME);
	snprintf(clientCRT, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory,
			AWS_IOT_CERTIFICATE_FILENAME);
	snprintf(clientKey, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory,
			AWS_IOT_PRIVATE_KEY_FILENAME);

	IOT_DEBUG("rootCA %s", rootCA);
	IOT_DEBUG("clientCRT %s", clientCRT);
	IOT_DEBUG("clientKey %s", clientKey);

	// Initialize IOT Client, some internal book-keeping
	ShadowInitParameters_t sp = ShadowInitParametersDefault;
	sp.pHost = AWS_IOT_MQTT_HOST;
	sp.port = AWS_IOT_MQTT_PORT;
	sp.pClientCRT = clientCRT;
	sp.pClientKey = clientKey;
	sp.pRootCA = rootCA;
	sp.enableAutoReconnect = false;
	sp.disconnectHandler = NULL;
	printf("Shadow Init");
	rc = aws_iot_shadow_init(&AWSMQTTclient, &sp);
	if(SUCCESS != rc) {
		printf("Shadow Connection Error");
		return rc;
	}

	// Do the TLSv1.2 handshake and establish the MQTT connection to AWS
	ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
	scp.pMyThingName = AWS_IOT_MY_THING_NAME;
	scp.pMqttClientId = AWS_IOT_MQTT_CLIENT_ID;
	scp.mqttClientIdLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);
	printf("Shadow Connect");
	rc = aws_iot_shadow_connect(&AWSMQTTclient, &scp);
	if (SUCCESS != rc) {
		printf("Shadow Connection Error");
		return rc;
	}

	// Enable Auto Reconnect functionality. Minimum and Maximum time of
	// Exponential backoff are set in aws_iot_config.h
	//      #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
	//    #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
	rc = aws_iot_shadow_set_autoreconnect_status(&AWSMQTTclient, true);
	if (SUCCESS != rc) {
		printf("Unable to set Auto Reconnect to true - %d", rc);
		return rc;
	}
	// Register the jsonStruct object
	jsonStruct_t deltaObject;
	deltaObject.pData = stringToEchoDelta;
	deltaObject.dataLength = SHADOW_MAX_SIZE_OF_RX_BUFFER;
	deltaObject.pKey = "state";
	deltaObject.type = SHADOW_JSON_OBJECT;
	deltaObject.cb = awsDeltaCallback;
	rc = aws_iot_shadow_register_delta(&AWSMQTTclient, &deltaObject);
	return rc;
}

// todo: better handling than this. Consider removing alltogether
static void shadowUpdateStatusCallback(const char *pThingName,
		ShadowActions_t action, Shadow_Ack_Status_t status,
		const char *pReceivedJsonDocument, void *pContextData)
{
	IOT_UNUSED(pThingName);
	IOT_UNUSED(action);
	IOT_UNUSED(pReceivedJsonDocument);
	IOT_UNUSED(pContextData);

	if(SHADOW_ACK_TIMEOUT == status) {
		printf("Update Timeout--\n");
	} else if(SHADOW_ACK_REJECTED == status) {
		printf("Update RejectedXX\n");
	} else if(SHADOW_ACK_ACCEPTED == status) {
		printf("Update Accepted !!\n");
	}

	fflush(stdout);
}
// Check all modules connected to the broker to make sure they're alive.
// Report update to AWS IOT. Currently only garage module exists.
static void checkModuleHealth(unsigned seconds) {
	// TODO: Consider limiting publishing to if there is a change in health only

	// Update garage status
	garageIsDead = GarageCheckIfDeadHW();
	// Let's save a bit of money and not just spam AWS
	if (seconds % 5 == 0)
		PublishToAWS(1, &garageSensorHealth);

	return;
}
/*------------------------------------------------------------------------------
 --|
 --| Public Function Bodies
 --|
 -----------------------------------------------------------------------------*/
// Called by anyone who wishes to publish data to hardware on the LAN.
extern void PublishToLAN(const char *topic, const char*msg)
{
	MQTTClient_message  mqttMsg = MQTTClient_message_initializer;
	mqttMsg.payloadlen = strlen(msg);
	mqttMsg.payload = (void*)msg;
	mqttMsg.qos = QOS;

	MQTTClient_publishMessage(LANMQTTclient, topic, &mqttMsg, NULL);
	return;
}
// Called by anyone who wishes to publish data to AWS. Connected modules (just
// garage at the moment) call this function to update their shadow in AWS IOT.
// This module (manager) also calls this to report system-level details to
// the AWS IOT cloud (loss of connectivity to a module).
#define MAX_LENGTH_OF_UPDATE_JSON_BUFFER 200
extern void PublishToAWS(uint8_t count, ...) {

	char jsonDocBuff[MAX_LENGTH_OF_UPDATE_JSON_BUFFER];
	size_t sizeOfJsonDocBuff = NELEMS(jsonDocBuff);

	va_list pArgs;
	va_start(pArgs, count);

	IoT_Error_t rc = FAILURE;

	rc = aws_iot_shadow_init_json_document(jsonDocBuff, sizeOfJsonDocBuff);

	if (SUCCESS != rc)
		return;

	rc = aws_iot_shadow_add_reported(jsonDocBuff, sizeOfJsonDocBuff, count,
			pArgs);

	if (SUCCESS != rc)
		return;

	rc = aws_iot_finalize_json_document(jsonDocBuff, sizeOfJsonDocBuff);

	if (SUCCESS != rc)
		return;
	// This var is involved in a temporary workaround for dealing with "nod idle" return code
	unsigned x = 0;
	do
	{
		// Not thread-safe apparently
		pthread_mutex_lock(&lock);
		rc = aws_iot_shadow_yield(&AWSMQTTclient, 200);
	    	pthread_mutex_unlock(&lock);

		if (NETWORK_ATTEMPTING_RECONNECT == rc) {
			sleep(1);
		}

		// Temporary workaround for "not idle" situation. I believe this is threading related. 
		if (MQTT_CLIENT_NOT_IDLE_ERROR == rc)
		{
			printf("Ran into not idle... %u times\n", ++x);
			sleep(2);
			if ( x > 50)
			{
				x = 0;
				aws_iot_shadow_disconnect(&AWSMQTTclient);
				awsMQTTInit();
				printf("RESTARTED\n");
				fflush(stdout);
				return;
			}

		}

	} while (SUCCESS != rc && NETWORK_RECONNECTED != rc);
	do
	{
	// Not thread-safe apparently
	    pthread_mutex_lock(&lock);
		rc = aws_iot_shadow_update(&AWSMQTTclient, AWS_IOT_MY_THING_NAME,
				jsonDocBuff, shadowUpdateStatusCallback, NULL, 4,
				true);
	    pthread_mutex_unlock(&lock);
		if (rc != SUCCESS)
			printf("Sent update. Returned %d\r", rc);

	} while (SUCCESS != rc);

	fflush(stdout);

	va_end(pArgs);

	return;
}
int main(void) {

	// Set up the AWS IOT interface
	IoT_Error_t awsMQTTret = awsMQTTInit();

	// Set up our LAN MQTT interface
	int lanMQTTret = lanMQTTInit();

	// We have two threads in this application and one call is not thread-safe
	if (pthread_mutex_init(&lock, NULL) != 0)
	        return EXIT_FAILURE;

	if (lanMQTTret != MQTTCLIENT_SUCCESS || awsMQTTret != SUCCESS)
		return EXIT_FAILURE;

	// Main loop. Note: once setup, MQTT callbacks dicate control flow.
	unsigned relativeSecs = 0;
	while (1)
	{
		relativeSecs++;
		
		// Not thread-safe apparently
		pthread_mutex_lock(&lock);
		aws_iot_shadow_yield(&AWSMQTTclient, 200);
		pthread_mutex_unlock(&lock);
		
		MQTTClient_yield();
		sleep(1);
		// Periodically check on module health
		checkModuleHealth(relativeSecs);
	}

	// Never reached
	//pthread_mutex_destroy(&lock);
	//return EXIT_SUCCESS;
}
