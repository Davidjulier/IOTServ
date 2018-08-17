/*
 * GarageShadow.h
 *
 *  Created on: May 25, 2018
 *      Author: Lenny
 */

#ifndef GARAGESHADOW_GARAGESHADOW_H_
#define GARAGESHADOW_GARAGESHADOW_H_

/*------------------------------------------------------------------------------
--|
--| Includes
--|
------------------------------------------------------------------------------*/
#include "aws_iot_mqtt_client_interface.h"
// To connect with the main manager
#include "../Manager.h"
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

// Input from external commands. May require a response.
extern void GarageHandleDataFromAWS(const char *pJsondataFromAWS);

// Input from actual hardware
extern int GarageHandleDataFromHW(char *topicName, char *message);

// This is the function our MQTT manager will call to figure out what we
// care to receive and publish fromt he MQTT interface. It will handle all of
// that work for us. This keeps this module from having to know the specifics
// of both of the MQTT interfaces that we use in this system.
extern stringList_structType* GarageGetTopics(topicType_enumType type);
extern void GarageGetTopics_free(stringList_structType *ret);

// Check if HW is alive
extern bool GarageCheckIfDeadHW();


#endif /* GARAGESHADOW_GARAGESHADOW_H_ */
