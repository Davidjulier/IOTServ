/*
 * GarageShadow.c
 *
 *  Created on: May 25, 2018
 *      Author: Lenny
 */


/*-----------------------------------------------------------------------------
--|
--| Includes
--|
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "../Switchs.h"
#include "../Utilities.h"
#include "GarageShadow.h"

#include "aws_iot_json_utils.h" // To parse aws data that comes in

/*-----------------------------------------------------------------------------
--|
--| Defines
--|
-----------------------------------------------------------------------------*/
// If nothing from HW for 2 seconds we are dead-in-the-water
#define TIMEOUT_IN_S 2

// Topic defines (must match what the HW sends out)
#define PUB_GARAGE_GENERAL "home/garage/general"
#define PUB_GARAGE_SENSOR "home/garage/sensor"
#define PUB_GARAGE_DEBUG "home/garage/debug"

#define SUB_GARAGE_CMD "home/garage/command"
/*-----------------------------------------------------------------------------
--|
--| Types
--|
-----------------------------------------------------------------------------*/
// Door state - Must match what the HW sends out
typedef enum
{
	DOOR_CLOSED,
	DOOR_OPENED,
	DOOR_UNKNOWN,
} doorState_enumType;

// System state - Must match what the HW sends out
typedef enum
{
	BOOTING, // no idea where door opened/closed measurements are
	CALIBRATING, // know where door opened OR door closed measurement is
	NOMINAL, // know where door opened AND door closed measurements are
} sysState_enumType;


// Debug info - Must match what the HW sends out
typedef struct
{
	// Sonar
	unsigned closedDist;
	unsigned openedDist;
	unsigned currDist;
	// Up time
	unsigned days;
	unsigned hours;
	unsigned minutes;
	// lan mqtt reconnects
	unsigned reconnects;
	// worst case software foreground frame time
	unsigned wcf;
	// This guy is NOT sent out by the hardware. Rather, the Rasp Pi Software
	// (this application) determines the state of this variable. Hardware
	// doesnt know if it died.
	bool garageIsDead;
}garageSensorDebugModel_t;

// Door
typedef struct
{
	sysState_enumType sysState;
	doorState_enumType doorState;

}garageSensorDoorModel_t;

// Our virtual representation of the garage monitor HW
typedef struct
{
	garageSensorDebugModel_t debug;
	garageSensorDoorModel_t sensor;
} garageShadow_t;

/*-----------------------------------------------------------------------------
--|
--| Private Data
--|
-----------------------------------------------------------------------------*/
static garageShadow_t virtualGarageShadow = {{0,0,0},{BOOTING,DOOR_UNKNOWN}};

 // Data for AWS IOT
static const jsonStruct_t openStatus = {
		 "open",
		 &virtualGarageShadow.sensor.doorState,
		 sizeof(unsigned),
		 SHADOW_JSON_INT32,
		 NULL,

 };
static const jsonStruct_t sysState = {
 		 "systemState",
 		 &virtualGarageShadow.sensor.sysState,
 		 sizeof(unsigned),
 		 SHADOW_JSON_INT32,
 		 NULL,
  };
static const jsonStruct_t dbgOpenStatus = {
 		 "dbgOpen",
 		 &virtualGarageShadow.debug.openedDist,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgClosedStatus = {
 		 "dbgClosed",
 		 &virtualGarageShadow.debug.closedDist,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgCurrentStatus = {
 		 "dbgCurrent",
 		 &virtualGarageShadow.debug.currDist,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgDays = {
 		 "dbgDays",
 		 &virtualGarageShadow.debug.days,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgHours = {
 		 "dbgHours",
 		 &virtualGarageShadow.debug.hours,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgMinutes = {
 		 "dbgMins",
 		 &virtualGarageShadow.debug.minutes,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgReconnects = {
 		 "dbgReconnects",
 		 &virtualGarageShadow.debug.reconnects,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
static const jsonStruct_t dbgWcf = {
 		 "dbgWcf",
 		 &virtualGarageShadow.debug.wcf,
 		 sizeof(unsigned),
 		 SHADOW_JSON_UINT32,
 		 NULL,
  };
// To see if we lost connection with our hw
static time_t timeOfLastPing = 0;
/*------------------------------------------------------------------------------
--|
--| Private Function Bodies
--|
------------------------------------------------------------------------------*/
// Update the internal representation fo the debug info
static bool updateDebugModelGarage(char *message){
	printf("%s!\n", message);
// Example String "Open:17 Close:29 Current:29 Days:0 Hours:0 Mins:1 Secs:37 Reconnects:0 WCF:10 "
	unsigned openedDist, closedDist, currDist, days, hours, mins, reconnects, wcf;
	char *token;
	char *rest = message;
	enum
	{
		OPEN,
		OPENVAL,
		CLOSE,
		CLOSEVAL,
		CURRENT,
		CURRENTVAL,
		UPTIME_DAYS,
		UPTIME_DAYSVAL,
		UPTIME_HOURS,
		UPTIME_HOURSVAL,
		UPTIME_MINS,
		UPTIME_MINSVAL,
		UPTIME_SECS,
		UPTIME_SECSVAL,
		LAN_MQTT_RECONNECTS,
		LAN_MQTT_RECONNECTSVAL,
		WORSTCASEFRAMETIME,
		WORSTCASEFRAMETIMEVAL,
		strIdx_size
	} strIdx;

	for (strIdx = OPEN; strIdx < strIdx_size; strIdx++)
	{
		// Note: strtok is destructive, but as the end consumer, we don't care.
		token = strtok_r(rest, " :", &rest);
		if (token)
		{
			// Todo: think about error checking strtoul. Because we control the
			// message to begin with, it's livable for now
			switch (strIdx)
			{
			case OPENVAL:
				openedDist = strtoul(token, NULL, 10);
				break;
			case CLOSEVAL:
				closedDist = strtoul(token, NULL, 10);
				break;
			case CURRENTVAL:
				currDist = strtoul(token, NULL, 10);
				break;
			case UPTIME_DAYSVAL:
				days = strtoul(token, NULL, 10);
				break;
			case UPTIME_HOURSVAL:
				hours = strtoul(token, NULL, 10);
				break;
			case UPTIME_MINSVAL:
				mins = strtoul(token, NULL, 10);
				break;
			case LAN_MQTT_RECONNECTSVAL:
				reconnects = strtoul(token, NULL, 10);
				break;
			case WORSTCASEFRAMETIMEVAL:
				wcf = strtoul(token, NULL, 10);
				break;
			default : // Ignore
				break;
			}
		}
	}

	// Now let's see if an update to ur shadow is needed
	bool updateWasNeeded = false;
	if ((virtualGarageShadow.debug.openedDist != openedDist)||
        (virtualGarageShadow.debug.closedDist != closedDist)||
		(virtualGarageShadow.debug.currDist != currDist)    ||
		(virtualGarageShadow.debug.days != days)||
		(virtualGarageShadow.debug.hours != hours)||
		(virtualGarageShadow.debug.minutes != mins)    ||
		(virtualGarageShadow.debug.reconnects != reconnects)||
		(virtualGarageShadow.debug.wcf != wcf))
		updateWasNeeded = true;

	// Update it regardless
	virtualGarageShadow.debug.openedDist = openedDist;
	virtualGarageShadow.debug.closedDist = closedDist;
	virtualGarageShadow.debug.currDist = currDist;
	virtualGarageShadow.debug.days = days;
	virtualGarageShadow.debug.hours = hours;
	virtualGarageShadow.debug.minutes = mins;
	virtualGarageShadow.debug.reconnects = reconnects;
	virtualGarageShadow.debug.wcf = wcf;
	return updateWasNeeded;
}
// Update the internal representation of the sensor
static bool updateSensorModelGarage(char *message)
{
	// Example string "Door:opened State:nominal"
	sysState_enumType sysState;
	doorState_enumType doorState;
	char *token;
	char *rest = message;
	enum
	{
		DOOR, DOORENUM, STATE, STATEENUM, strIdx_size
	} strIdx;

	for (strIdx = DOOR; strIdx < strIdx_size; strIdx++)
	{
		// Note: strtok is destructive, but as the end consumer, we don't care.
		token = strtok_r(rest, " :", &rest);
		if (token)
		{
			switch (strIdx)
			{
			case DOORENUM:
				// Syntactic sugar (see switchs.h)
				switchs(token)
						{
						icases("opened")
							doorState = DOOR_OPENED;
							break;
						icases("closed")
							doorState = DOOR_CLOSED;
							break;
						defaults
							doorState = DOOR_UNKNOWN;
							break;
						}switchs_end
				;
				break;
			case STATEENUM:
				// Syntactic sugar (see switchs.h)
				switchs(token)
						{
						icases("booting")
							sysState = BOOTING;
							break;
						icases("calibrating")
							sysState = CALIBRATING;
							break;
						defaults
							sysState = NOMINAL;
							break;
						}switchs_end
				;
				break;
			default: // Ignore
				break;
			}
		}
	}

	// Now let's see if an update to our shadow is needed
	bool updateWasNeeded = false;
	if ((virtualGarageShadow.sensor.doorState != doorState)	||
		(virtualGarageShadow.sensor.sysState != sysState))
		updateWasNeeded = true;

	// Update it regardless
	virtualGarageShadow.sensor.doorState = doorState;
	virtualGarageShadow.sensor.sysState = sysState;

	return updateWasNeeded;

}

/*------------------------------------------------------------------------------
--|
--| Public Function Bodies
--|
------------------------------------------------------------------------------*/

// Returns a list of topics we want to be subscribed to by the manager
extern stringList_structType* GarageGetTopics(topicType_enumType type)
{
	// todo: error check malloc
	stringList_structType *retVal = malloc(sizeof(*retVal));

	switch (type) {
	case MODULE_SUBSCRIBES:
		retVal->numTopics = 1;
		retVal->topicList = malloc(retVal->numTopics * sizeof(char*));

		retVal->topicList[0] = SUB_GARAGE_CMD;
		break;
	case MODULE_PUBLISHES:
		retVal->numTopics = 3;
		retVal->topicList = malloc(retVal->numTopics * sizeof(char*));

		retVal->topicList[0] = PUB_GARAGE_GENERAL;
		retVal->topicList[1] = PUB_GARAGE_SENSOR;
		retVal->topicList[2] = PUB_GARAGE_DEBUG;
		break;
	default :
		// Caller should never do this
		retVal->numTopics = 0;
		break;
	}


	return retVal;

}
// Caller must call this after GarageGetTopics
extern void GarageGetTopics_free(stringList_structType *ret)
{
	free(ret->topicList);
	free(ret);
	return;
}

// Handle data coming from the real hardware to this module
extern int GarageHandleDataFromHW(char *topicName, char *message)
{
	// MQTT API dictates we return the  following
	const int MSG_NOT_HANDLED = 0;
	const int MSG_HANDLED = 1;

	int retVal = MSG_HANDLED;
	bool shadowWasUpdated = false;
	// Syntactic sugar (see switchs.h)
	switchs(topicName)
			{
			icases(PUB_GARAGE_DEBUG)
				shadowWasUpdated |= updateDebugModelGarage(message);
				break;
			icases(PUB_GARAGE_SENSOR)
				shadowWasUpdated |= updateSensorModelGarage(message);
				break;
			icases(PUB_GARAGE_GENERAL)
				// Ignore for now
				break;
			defaults
				retVal = MSG_NOT_HANDLED;
				break;
			}switchs_end;

// If we had to update our internal shadow, we must update the cloud one too
	if (shadowWasUpdated)
	{
		printf("Lennylenny\n");
		// Split up into 2 transmissions (turns out we'll truncate the JSON
		// buffer eventually as we're right on the line of 200 limit if all
		// at once)
		PublishToAWS(5, &dbgCurrentStatus,
						&dbgClosedStatus,
						&dbgOpenStatus,
						&dbgDays,
						&dbgHours);
		PublishToAWS(5,
		                &dbgMinutes,
						&dbgReconnects,
						&dbgWcf,
						&sysState,
						&openStatus);
	}

	// Update time of last ping so we know if we died
	timeOfLastPing = time(NULL);
	return retVal;

}
// Handle data coming from the cloud to this module
extern void GarageHandleDataFromAWS(const char *pJsondataFromAWS) {
	jsmn_parser parser;
	jsmn_init(&parser);
	// Seems big enough for now
	jsmntok_t tokens[32];

	// Timestamp of command received.
	const char* CMD_TIMESTAMP = "timestamp";
	static unsigned cmdTimestamp = 0;
	unsigned currentTimestamp = 0;

	// Garage only cares about 'open' being received
	const char* GARAGE_CMD = "open";
	unsigned openVal = UINT_MAX;


	jsmn_parse(&parser, pJsondataFromAWS, strlen(pJsondataFromAWS), tokens,
			NELEMS(tokens));
	for (int i = 0; i < NELEMS(tokens); i++)
	{
		if (tokens[i].type == JSMN_STRING)
		{
			// Get open value
			if (strncmp(pJsondataFromAWS + tokens[i].start, GARAGE_CMD,
					strlen(GARAGE_CMD)) == 0)
			{
				parseUnsignedInteger32Value(&openVal, pJsondataFromAWS,
						&tokens[i + 1]);
			}
			// Get timestamp value
			else if (strncmp(pJsondataFromAWS + tokens[i].start, CMD_TIMESTAMP,
					strlen(CMD_TIMESTAMP)) == 0)
			{
				parseUnsignedInteger32Value(&currentTimestamp, pJsondataFromAWS,
						&tokens[i + 1]);
			}
		}
	}

	// If this command hasn't been processed yet
	if (currentTimestamp != cmdTimestamp)
	{
		// If 'close' command, send it down to the real hardware
		if (openVal == 0)
		{
			PublishToLAN(SUB_GARAGE_CMD, "close");
			printf("Publishing close\n");

		}
		// If 'open' command, send it down to the real hardware
		if (openVal == 1)
		{
			PublishToLAN(SUB_GARAGE_CMD, "open");
			printf("Publishing open\n");
		}
		cmdTimestamp = currentTimestamp;
	}
}

// Returns true if HW hasn't responded in its alloted time
extern bool GarageCheckIfDeadHW()
{

	return (difftime(time(NULL), timeOfLastPing)  > TIMEOUT_IN_S);


}
