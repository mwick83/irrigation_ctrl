#ifndef GLOBAL_COMPONENTS_H
#define GLOBAL_COMPONENTS_H

#include "hardwareConfig.h"

#ifdef __cplusplus

#include "serialPacketizer.h"
#include "fillSensorProtoHandler.h"
#include "timeSystem.h"
#include "powerManager.h"
#include "irrigationController.h"
#include "mqttManager.h"

extern FillSensorPacketizer fillSensorPacketizer;
extern FillSensorProtoHandler<FillSensorPacketizer> fillSensor;

extern PowerManager pwrMgr;

extern MqttManager mqttMgr;

#endif

#endif /* GLOBAL_COMPONENTS_H */
