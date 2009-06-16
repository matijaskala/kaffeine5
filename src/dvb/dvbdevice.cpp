/*
 * dvbdevice.cpp
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

#include "dvbdevice.h"

#include <unistd.h>
#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QThread>
#include <KDebug>
#include "dvbmanager.h"

class DvbDummyFilter : public DvbPidFilter
{
public:
	void processData(const char [188]) { }
};

struct DvbFilterData
{
	char packets[21][188];
	int count;
	DvbFilterData *next;
};

class DvbFilterInternal
{
public:
	explicit DvbFilterInternal(int pid_) : pid(pid_) { }
	~DvbFilterInternal() { }

	bool operator<(const DvbFilterInternal &x) const
	{
		return pid < x.pid;
	}

	int pid;
	QList<DvbPidFilter *> filters;
};

static bool operator<(const DvbFilterInternal &x, int y)
{
	return x.pid < y;
}

static bool operator<(int x, const DvbFilterInternal &y)
{
	return x < y.pid;
}

DvbDevice::DvbDevice(DvbBackendDevice *backendDevice_, QObject *parent) : QObject(parent),
	backendDevice(backendDevice_), deviceState(DeviceIdle), isAuto(false)
{
	backendDevice->buffer = this;

	connect(&frontendTimer, SIGNAL(timeout()), this, SLOT(frontendEvent()));

	dummyFilter = new DvbDummyFilter;

	currentUnused = new DvbFilterData;
	currentUnused->next = currentUnused;
	currentUsed = currentUnused;

	usedBuffers = 0;
	totalBuffers = 1;
}

DvbDevice::~DvbDevice()
{
	for (int i = 0; i < totalBuffers; ++i) {
		DvbFilterData *temp = currentUnused->next;
		delete currentUnused;
		currentUnused = temp;
	}

	delete dummyFilter;
}

void DvbDevice::tune(const DvbTransponder &transponder)
{
	if (transponder->getTransmissionType() != DvbTransponderBase::DvbS) {
		if (backendDevice->tune(transponder)) {
			setDeviceState(DeviceTuning);
			frontendTimeout = config->timeout;
			frontendTimer.start(100);
		} else {
			setDeviceState(DeviceTuningFailed);
		}

		return;
	}

	bool moveRotor = false;

	const DvbSTransponder *dvbSTransponder = transponder->getDvbSTransponder();
	Q_ASSERT(dvbSTransponder != NULL);

	// parameters

	bool horPolar = (dvbSTransponder->polarization == DvbSTransponder::Horizontal) ||
			(dvbSTransponder->polarization == DvbSTransponder::CircularLeft);

	int frequency = dvbSTransponder->frequency;
	bool highBand = false;

	if (config->switchFrequency != 0) {
		// dual LO (low / high)
		if (frequency < config->switchFrequency) {
			frequency = abs(frequency - config->lowBandFrequency);
		} else {
			frequency = abs(frequency - config->highBandFrequency);
			highBand = true;
		}
	} else if (config->highBandFrequency != 0) {
		// single LO (horizontal / vertical)
		if (horPolar) {
			frequency = abs(frequency - config->lowBandFrequency);
		} else {
			frequency = abs(frequency - config->highBandFrequency);
		}
	} else {
		// single LO
		frequency = abs(frequency - config->lowBandFrequency);
	}

	// tone off

	backendDevice->setTone(DvbBackendDevice::ToneOff);

	// horizontal / circular left --> 18V ; vertical / circular right --> 13V

	backendDevice->setVoltage(horPolar ? DvbBackendDevice::Voltage18V :
				  DvbBackendDevice::Voltage13V);

	// diseqc / rotor

	usleep(15000);

	switch (config->configuration) {
	case DvbConfigBase::DiseqcSwitch: {
		char cmd[] = { 0xe0, 0x10, 0x38, 0x00 };
		cmd[3] = 0xf0 | (config->lnbNumber << 2) | (horPolar ? 2 : 0) | (highBand ? 1 : 0);
		backendDevice->sendMessage(cmd, sizeof(cmd));
		usleep(15000);

		backendDevice->sendBurst(((config->lnbNumber & 0x1) == 0) ?
			DvbBackendDevice::BurstMiniA : DvbBackendDevice::BurstMiniB);
		usleep(15000);
		break;
	    }

	case DvbConfigBase::UsalsRotor: {
		QString source = config->scanSource;
		source.remove(0, source.lastIndexOf('-') + 1);

		bool ok = false;
		double position = 0;

		if (source.endsWith('E')) {
			source.chop(1);
			position = source.toDouble(&ok);
		} else if (source.endsWith('W')) {
			source.chop(1);
			position = -source.toDouble(&ok);
		}

		if (!ok) {
			kWarning() << "couldn't extract orbital position";
		}

		double longitudeDiff = DvbManager::getLongitude() - position;

		double latRad = DvbManager::getLatitude() * M_PI / 180;
		double longDiffRad = longitudeDiff * M_PI / 180;
		double temp = cos(latRad) * cos(longDiffRad);
		double temp2 = -sin(latRad) * cos(longDiffRad) / sqrt(1 - temp * temp);

		// deal with corner cases
		if (temp2 < -1) {
			temp2 = -1;
		} else if (temp2 > 1) {
			temp2 = 1;
		} else if (temp2 != temp2) {
			temp2 = 1;
		}

		double azimuth = acos(temp2) * 180 / M_PI;

		if (((longitudeDiff > 0) && (longitudeDiff < 180)) || (longitudeDiff < -180)) {
			azimuth = 360 - azimuth;
		}

		int value = (azimuth * 16) + 0.5;

		if (value == (360 * 16)) {
			value = 0;
		}

		char cmd[] = { 0xe0, 0x31, 0x6e, value / 256, value % 256 };
		backendDevice->sendMessage(cmd, sizeof(cmd));
		usleep(15000);
		moveRotor = true;
		break;
	    }

	case DvbConfigBase::PositionsRotor: {
		char cmd[] = { 0xe0, 0x31, 0x6b, config->lnbNumber };
		backendDevice->sendMessage(cmd, sizeof(cmd));
		usleep(15000);
		moveRotor = true;
		break;
	    }
	}

	// low band --> tone off ; high band --> tone on

	backendDevice->setTone(highBand ? DvbBackendDevice::ToneOn : DvbBackendDevice::ToneOff);

	// tune

	DvbSTransponder *intermediate = new DvbSTransponder(*dvbSTransponder);
	intermediate->frequency = frequency;

	if (backendDevice->tune(DvbTransponder(intermediate))) {
		if (!moveRotor) {
			setDeviceState(DeviceTuning);
			frontendTimeout = config->timeout;
		} else {
			setDeviceState(DeviceRotorMoving);
			frontendTimeout = 15000;
		}

		frontendTimer.start(100);
	} else {
		setDeviceState(DeviceTuningFailed);
	}
}

void DvbDevice::autoTune(const DvbTransponder &transponder)
{
	if (transponder->getTransmissionType() != DvbTransponderBase::DvbT) {
		kWarning() << "can't handle != DVB-T";
		return;
	}

	isAuto = true;
	autoTTransponder = new DvbTTransponder(*transponder->getDvbTTransponder());
	autoTransponder = DvbTransponder(autoTTransponder);
	capabilities = backendDevice->getCapabilities();

	// we have to iterate over unsupported AUTO values

	if ((capabilities & DvbBackendDevice::DvbTFecAuto) == 0) {
		autoTTransponder->fecRateHigh = DvbTTransponder::Fec2_3;
	}

	if ((capabilities & DvbBackendDevice::DvbTGuardIntervalAuto) == 0) {
		autoTTransponder->guardInterval = DvbTTransponder::GuardInterval1_8;
	}

	if ((capabilities & DvbBackendDevice::DvbTModulationAuto) == 0) {
		autoTTransponder->modulation = DvbTTransponder::Qam64;
	}

	if ((capabilities & DvbBackendDevice::DvbTTransmissionModeAuto) == 0) {
		autoTTransponder->transmissionMode = DvbTTransponder::TransmissionMode8k;
	}

	tune(autoTransponder);
}

DvbTransponder DvbDevice::getAutoTransponder() const
{
	// FIXME query back information like frequency - tuning parameters - ...
	return autoTransponder;
}

void DvbDevice::release()
{
	if ((deviceState == DeviceNotReady) || (deviceState == DeviceIdle)) {
		return;
	}

	// stop waiting for tuning
	frontendTimer.stop();

	backendDevice->release();

	isAuto = false;
	setDeviceState(DeviceIdle);
}

int DvbDevice::getSignal()
{
	return backendDevice->getSignal();
}

int DvbDevice::getSnr()
{
	return backendDevice->getSnr();
}

bool DvbDevice::isTuned()
{
	return backendDevice->isTuned();
}

bool DvbDevice::addPidFilter(int pid, DvbPidFilter *filter)
{
	QList<DvbFilterInternal>::iterator it = qLowerBound(pendingFilters.begin(),
		pendingFilters.end(), pid);

	if ((it == pendingFilters.end()) || (it->pid != pid)) {
		if (!backendDevice->addPidFilter(pid)) {
			return false;
		}

		it = pendingFilters.insert(it, DvbFilterInternal(pid));
	}

	if (it->filters.contains(filter)) {
		kWarning() << "using the same filter for the same pid more than once";
		return true;
	}

	it->filters.append(filter);

	it = qLowerBound(activeFilters.begin(), activeFilters.end(), pid);

	if ((it == activeFilters.end()) || (it->pid != pid)) {
		it = activeFilters.insert(it, DvbFilterInternal(pid));
	}

	it->filters.append(filter);
	return true;
}

void DvbDevice::removePidFilter(int pid, DvbPidFilter *filter)
{
	QList<DvbFilterInternal>::iterator it = qBinaryFind(pendingFilters.begin(),
		pendingFilters.end(), pid);

	if (it == pendingFilters.end()) {
		kWarning() << "trying to remove a nonexistant filter";
		return;
	}

	if (!it->filters.removeOne(filter)) {
		kWarning() << "trying to remove a nonexistant filter";
		return;
	}

	if (it->filters.isEmpty()) {
		backendDevice->removePidFilter(pid);
		pendingFilters.erase(it);
	}

	it = qBinaryFind(activeFilters.begin(), activeFilters.end(), pid);
	it->filters.replace(it->filters.indexOf(filter), dummyFilter);
}

void DvbDevice::frontendEvent()
{
	if (backendDevice->isTuned()) {
		kDebug() << "tuning succeeded";
		frontendTimer.stop();
		setDeviceState(DeviceTuned);
		return;
	}

	// FIXME progress bar when moving rotor

	frontendTimeout -= 100;

	if (frontendTimeout <= 0) {
		frontendTimer.stop();

		if (!isAuto) {
			kWarning() << "tuning failed";
			setDeviceState(DeviceTuningFailed);
			return;
		}

		int signal = backendDevice->getSignal();

		if ((signal != -1) && (signal < 15)) {
			// signal too weak
			kWarning() << "tuning failed";
			setDeviceState(DeviceTuningFailed);
			return;
		}

		bool carry = true;

		if (carry && ((capabilities & DvbBackendDevice::DvbTFecAuto) == 0)) {
			switch (autoTTransponder->fecRateHigh) {
			case DvbTTransponder::Fec2_3:
				autoTTransponder->fecRateHigh = DvbTTransponder::Fec3_4;
				carry = false;
				break;
			case DvbTTransponder::Fec3_4:
				autoTTransponder->fecRateHigh = DvbTTransponder::Fec1_2;
				carry = false;
				break;
			case DvbTTransponder::Fec1_2:
				autoTTransponder->fecRateHigh = DvbTTransponder::Fec5_6;
				carry = false;
				break;
			case DvbTTransponder::Fec5_6:
				autoTTransponder->fecRateHigh = DvbTTransponder::Fec7_8;
				carry = false;
				break;
			case DvbTTransponder::Fec4_5:
			case DvbTTransponder::Fec6_7:
			case DvbTTransponder::Fec7_8:
			case DvbTTransponder::Fec8_9:
			case DvbTTransponder::FecNone:
			case DvbTTransponder::FecAuto:
				autoTTransponder->fecRateHigh = DvbTTransponder::Fec2_3;
				break;
			}
		}

		if (carry && ((capabilities & DvbBackendDevice::DvbTGuardIntervalAuto) == 0)) {
			switch (autoTTransponder->guardInterval) {
			case DvbTTransponder::GuardInterval1_8:
				autoTTransponder->guardInterval = DvbTTransponder::GuardInterval1_32;
				carry = false;
				break;
			case DvbTTransponder::GuardInterval1_32:
				autoTTransponder->guardInterval = DvbTTransponder::GuardInterval1_4;
				carry = false;
				break;
			case DvbTTransponder::GuardInterval1_4:
				autoTTransponder->guardInterval = DvbTTransponder::GuardInterval1_16;
				carry = false;
				break;
			case DvbTTransponder::GuardInterval1_16:
			case DvbTTransponder::GuardIntervalAuto:
				autoTTransponder->guardInterval = DvbTTransponder::GuardInterval1_8;
				break;
			}
		}

		if (carry && ((capabilities & DvbBackendDevice::DvbTModulationAuto) == 0)) {
			switch (autoTTransponder->modulation) {
			case DvbTTransponder::Qam64:
				autoTTransponder->modulation = DvbTTransponder::Qam16;
				carry = false;
				break;
			case DvbTTransponder::Qam16:
				autoTTransponder->modulation = DvbTTransponder::Qpsk;
				carry = false;
				break;
			case DvbTTransponder::Qpsk:
			case DvbTTransponder::ModulationAuto:
				autoTTransponder->modulation = DvbTTransponder::Qam64;
				break;
			}
		}

		if (carry && ((capabilities & DvbBackendDevice::DvbTTransmissionModeAuto) == 0)) {
			switch (autoTTransponder->transmissionMode) {
			case DvbTTransponder::TransmissionMode8k:
				autoTTransponder->transmissionMode = DvbTTransponder::TransmissionMode2k;
				carry = false;
				break;
			case DvbTTransponder::TransmissionMode2k:
			case DvbTTransponder::TransmissionModeAuto:
				autoTTransponder->transmissionMode = DvbTTransponder::TransmissionMode8k;
				break;
			}
		}

		if (!carry) {
			deviceState = DeviceTuningFailed;
			tune(autoTransponder);
		} else {
			kWarning() << "tuning failed";
			setDeviceState(DeviceTuningFailed);
		}
	}
}

void DvbDevice::setDeviceState(DeviceState newState)
{
	deviceState = newState;
	emit stateChanged();
}

int DvbDevice::size()
{
	return 21 * 188;
}

char *DvbDevice::getCurrent()
{
	return currentUnused->packets[0];
}

void DvbDevice::submitCurrent(int packets)
{
	currentUnused->count = packets;

	if ((usedBuffers + 1) >= totalBuffers) {
		DvbFilterData *newBuffer = new DvbFilterData;
		newBuffer->next = currentUnused->next;
		currentUnused->next = newBuffer;
		++totalBuffers;
	}

	currentUnused = currentUnused->next;

	if (usedBuffers.fetchAndAddOrdered(1) == 0) {
		QCoreApplication::postEvent(this, new QEvent(QEvent::User));
	}
}

void DvbDevice::customEvent(QEvent *)
{
	Q_ASSERT(usedBuffers >= 1);

	while (true) {
		for (int i = 0; i < currentUsed->count; ++i) {
			char *packet = currentUsed->packets[i];

			if ((packet[1] & 0x80) != 0) {
				// transport error indicator
				continue;
			}

			int pid = ((static_cast<unsigned char>(packet[1]) << 8) |
				static_cast<unsigned char>(packet[2])) & ((1 << 13) - 1);

			QList<DvbFilterInternal>::const_iterator it = qBinaryFind(
				activeFilters.constBegin(), activeFilters.constEnd(), pid);

			if (it == activeFilters.constEnd()) {
				continue;
			}

			const DvbFilterInternal &internalFilter = *it;
			int filterSize = internalFilter.filters.size();

			for (int j = 0; j < filterSize; ++j) {
				internalFilter.filters.at(j)->processData(packet);
			}
		}

		currentUsed = currentUsed->next;

		if (usedBuffers.fetchAndAddOrdered(-1) == 1) {
			break;
		}
	}

	activeFilters = pendingFilters;
}
