/*
 * Manager.h
 *
 *  Created on: Jun 1, 2018
 *      Author: Lenny
 */

#ifndef MANAGER_H_
#define MANAGER_H_

/*------------------------------------------------------------------------------
--|
--| Includes
--|
------------------------------------------------------------------------------*/
// Contains the JSON struct modules (just garage at the moment) need to produce
// in order to talk to AWS IOT (i.e. update the shadow correctly)
#include "aws_iot_shadow_json.h"

/*------------------------------------------------------------------------------
--|
--| Defines
--|
------------------------------------------------------------------------------*/

/* None */

/*------------------------------------------------------------------------------
--|
--| Types
--|
------------------------------------------------------------------------------*/
// All modules, (currently just garage) will need to use these two types to
// tell manager which topics they publish and subscribe to and which delta
// parameters they would like to take action on
typedef struct
{
	unsigned numTopics;
	char **topicList;
} stringList_structType;
// Used to select a topic
typedef enum
{
	MODULE_SUBSCRIBES,// Module subscribes to these topics
	MODULE_PUBLISHES, // Module publishes these topics to broker
} topicType_enumType;

/*------------------------------------------------------------------------------
--|
--| Constants
--|
------------------------------------------------------------------------------*/

/* None */

/*------------------------------------------------------------------------------
--|
--| Function Specifications
--|
------------------------------------------------------------------------------*/
// All modules (currently just garage) will utilize this function if they want
// to update AWS IOT with some data
extern void PublishToAWS(uint8_t count, ...);
// All modules (currently just garage) will utilize this function if they want
// to update their shadow hardware (the real deal) with some data
extern void PublishToLAN(const char *topic, const char*msg);


#endif /* MANAGER_H_ */
