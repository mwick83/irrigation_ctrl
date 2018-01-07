#ifndef GLOBAL_COMPONENTS_H
#define GLOBAL_COMPONENTS_H

#include "hardwareConfig.h"

#ifdef __cplusplus

#include "serialPacketizer.h"
#include "fillSensorProtoHandler.h"
#include "timeSystem.h"
#include "powerManager.h"
#include "irrigationController.h"

extern FillSensorPacketizer fillSensorPacketizer;
extern FillSensorProtoHandler<FillSensorPacketizer> fillSensor;

extern PowerManager pwrMgr;

#endif

#endif /* GLOBAL_COMPONENTS_H */
