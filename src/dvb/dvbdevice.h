/*
 * dvbdevice.h
 *
 * Copyright (C) 2007-2009 Christoph Pfister <christophpfister@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef DVBDEVICE_H
#define DVBDEVICE_H

#include <QTimer>
#include "dvbchannel.h"
#include "dvbconfig.h"

class DvbBackendDevice;
class DvbFilterData;
class DvbFilterInternal;
class DvbManager;

class DvbAbstractDeviceBuffer
{
public:
	DvbAbstractDeviceBuffer() { }
	virtual ~DvbAbstractDeviceBuffer() { }

	// all those functions must be callable from a QThread

	virtual int size() = 0; // must be a multiple of 188
	virtual char *getCurrent() = 0;
	virtual void submitCurrent(int packets) = 0;
};

class DvbBackendDevice
{
public:
	enum TransmissionType {
		DvbC = (1 << 0),
		DvbS = (1 << 1),
		DvbT = (1 << 2),
		Atsc = (1 << 3)
	};

	Q_DECLARE_FLAGS(TransmissionTypes, TransmissionType)

	enum Capability {
		DvbTModulationAuto		= (1 << 0),
		DvbTFecAuto			= (1 << 1),
		DvbTTransmissionModeAuto	= (1 << 2),
		DvbTGuardIntervalAuto		= (1 << 3)
	};

	Q_DECLARE_FLAGS(Capabilities, Capability);

	enum SecTone {
		ToneOff = 0,
		ToneOn  = 1
	};

	enum SecVoltage {
		Voltage13V = 0,
		Voltage18V = 1
	};

	enum SecBurst {
		BurstMiniA = 0,
		BurstMiniB = 1
	};

	DvbBackendDevice() : buffer(NULL) { }
	virtual ~DvbBackendDevice() { }

	virtual QString getDeviceId() = 0;
	virtual QString getFrontendName() = 0;
	virtual TransmissionTypes getTransmissionTypes() = 0;
	virtual Capabilities getCapabilities() = 0;
	virtual bool acquire() = 0;
	virtual bool setTone(SecTone tone) = 0;
	virtual bool setVoltage(SecVoltage voltage) = 0;
	virtual bool sendMessage(const char *message, int length) = 0;
	virtual bool sendBurst(SecBurst burst) = 0;
	virtual bool tune(const DvbTransponder &transponder) = 0;
	virtual int getSignal() = 0;
	virtual int getSnr() = 0;
	virtual bool isTuned() = 0;
	virtual bool addPidFilter(int pid) = 0;
	virtual void removePidFilter(int pid) = 0;
	virtual void release() = 0;

	DvbAbstractDeviceBuffer *buffer;
};

class DvbPidFilter
{
public:
	DvbPidFilter() { }
	virtual ~DvbPidFilter() { }

	virtual void processData(const char data[188]) = 0;
};

class DvbDevice : public QObject, private DvbAbstractDeviceBuffer
{
	Q_OBJECT
public:
	enum DeviceState
	{
		DeviceNotReady,
		DeviceIdle,
		DeviceRotorMoving,
		DeviceTuning,
		DeviceTuningFailed,
		DeviceTuned
	};

	DvbDevice(DvbBackendDevice *backendDevice_, QObject *parent);
	DvbDevice(int deviceIndex_, QObject *parent);
	~DvbDevice();

	DvbBackendDevice *getBackendDevice()
	{
		return backendDevice;
	}

	DeviceState getDeviceState() const
	{
		return deviceState;
	}

	DvbBackendDevice::TransmissionTypes getTransmissionTypes() const
	{
		Q_ASSERT(deviceState != DeviceNotReady);
		return backendDevice->getTransmissionTypes();
	}

	QString getDeviceId() const
	{
		Q_ASSERT(deviceState != DeviceNotReady);
		return backendDevice->getDeviceId();
	}

	QString getFrontendName() const
	{
		Q_ASSERT(deviceState != DeviceNotReady);
		return backendDevice->getFrontendName();
	}

	bool acquire()
	{
		return backendDevice->acquire();
	}

	void tune(const DvbTransponder &transponder);
	void autoTune(const DvbTransponder &transponder);
	DvbTransponder getAutoTransponder() const;
	void release();

	/*
	 * signal and SNR are scaled from 0 to 100
	 * the device has to be tuned / tuning when you call one of these functions
	 */

	int getSignal();
	int getSnr();
	bool isTuned();

	/*
	 * you can use the same filter object for different pids
	 */

	bool addPidFilter(int pid, DvbPidFilter *filter);
	void removePidFilter(int pid, DvbPidFilter *filter);

	/*
	 * assigned by DvbManager::requestDevice()
	 */

	DvbConfig config;

signals:
	void stateChanged();

private slots:
	void frontendEvent();

private:
	void setDeviceState(DeviceState newState);

	int size();
	char *getCurrent();
	void submitCurrent(int packets);
	void customEvent(QEvent *);

	DvbBackendDevice *backendDevice;
	DeviceState deviceState;

	int frontendTimeout;
	QTimer frontendTimer;
	QList<DvbFilterInternal> activeFilters;
	QList<DvbFilterInternal> pendingFilters;
	DvbPidFilter *dummyFilter;

	bool isAuto;
	DvbTTransponder *autoTTransponder;
	DvbTransponder autoTransponder;
	DvbBackendDevice::Capabilities capabilities;

	DvbFilterData *currentUnused;
	DvbFilterData *currentUsed;
	QAtomicInt usedBuffers;
	int totalBuffers;
};

#endif /* DVBDEVICE_H */
