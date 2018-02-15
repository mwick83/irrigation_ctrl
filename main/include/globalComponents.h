#ifndef GLOBAL_COMPONENTS_H
#define GLOBAL_COMPONENTS_H

#include "hardwareConfig.h"

#ifdef __cplusplus

#include "serialPacketizer.h"
#include "fillSensorProtoHandler.h"
#include "timeSystem.h"
#include "powerManager.h"
#include "mqttManager.h"
#include "irrigationPlanner.h"

extern FillSensorPacketizer fillSensorPacketizer;
extern FillSensorProtoHandler<FillSensorPacketizer> fillSensor;

extern PowerManager pwrMgr;

extern MqttManager mqttMgr;

extern IrrigationPlanner irrigPlanner;

#endif

#endif /* GLOBAL_COMPONENTS_H */
