/*
* Copyright (C) 2016-2026, L-Acoustics and its contributors

* This file is part of LA_avdecc.

* LA_avdecc is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* LA_avdecc is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.

* You should have received a copy of the GNU Lesser General Public License
* along with LA_avdecc.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
* @file listenerCapabilityDelegate.cpp
* @author Christophe Calmejane
*
* @brief Listener entity-responder capability delegate (3SB software-mode P2).
*        See listenerCapabilityDelegate.hpp for scope notes.
*/

#include "la/avdecc/utils.hpp"
#include "la/avdecc/internals/aggregateEntity.hpp" // setListenerBindObserver declaration (LA_AVDECC_API export)

#include "listenerCapabilityDelegate.hpp"
#include "protocol/protocolAemPayloads.hpp"
#include "protocol/protocolMvuPayloads.hpp"

#include <algorithm>
#include <exception>

namespace la
{
namespace avdecc
{
namespace entity
{
/* ************************************************************************** */
/* Listener bind-observer registry (software-mode P2)                         */
/* ************************************************************************** */
// Side-channel from the daemon (which owns the AAF receiver) to the listener
// CapabilityDelegate (constructed internally by AggregateEntity). Same registry
// pattern + lifetime as the talker observers. Keyed by the raw EntityID value.
namespace
{
std::mutex& listenerRegistryMutex() noexcept
{
	static std::mutex s_mutex;
	return s_mutex;
}
std::unordered_map<UniqueIdentifier::value_type, ListenerBindObserver>& bindObserverRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, ListenerBindObserver> s_registry;
	return s_registry;
}
// 3SB #226: registry for the listener STREAM_INPUT counters provider (live receive-engine lock /
// frames / seq-errors), so GET_COUNTERS reports real media-lock state instead of a static stub.
std::unordered_map<UniqueIdentifier::value_type, ListenerCountersProvider>& listenerCountersProviderRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, ListenerCountersProvider> s_registry;
	return s_registry;
}
// 3SB #226: registry of LIVE listener delegates (added in the ctor, removed in the dtor), so the
// daemon can push an unsolicited STREAM_INPUT GET_COUNTERS notification by entityID at RUNTIME (after
// construction) when a stream's media-lock state changes — controllers read counters once at
// enumeration then rely on unsolicited updates.
std::unordered_map<UniqueIdentifier::value_type, listener::CapabilityDelegate*>& liveListenerDelegateRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, listener::CapabilityDelegate*> s_registry;
	return s_registry;
}
} // namespace

void LA_AVDECC_CALL_CONVENTION setListenerBindObserver(UniqueIdentifier const entityID, ListenerBindObserver observer) noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	bindObserverRegistry()[entityID.getValue()] = std::move(observer);
}

void LA_AVDECC_CALL_CONVENTION setListenerCountersProvider(UniqueIdentifier const entityID, ListenerCountersProvider provider) noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	listenerCountersProviderRegistry()[entityID.getValue()] = std::move(provider);
}

void LA_AVDECC_CALL_CONVENTION notifyListenerStreamInputCountersChanged(UniqueIdentifier const entityID, std::uint16_t const streamIndex, model::DescriptorCounterValidFlag const validCounters, model::DescriptorCounters const& counters) noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	auto& registry = liveListenerDelegateRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it != registry.end() && it->second != nullptr)
	{
		it->second->pushStreamInputCountersNotification(streamIndex, validCounters, counters);
	}
}

namespace listener
{
namespace
{
// Take (read + erase) the registered bind observer for an entity, or empty if none.
ListenerBindObserver takeListenerBindObserver(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	auto& registry = bindObserverRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto observer = std::move(it->second);
	registry.erase(it);
	return observer;
}
// Take (read + erase) the registered STREAM_INPUT counters provider for an entity, or empty if none.
ListenerCountersProvider takeListenerCountersProvider(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	auto& registry = listenerCountersProviderRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto provider = std::move(it->second);
	registry.erase(it);
	return provider;
}
} // namespace

/* ************************************************************************** */
/* Exceptions                                                                 */
/* ************************************************************************** */
class InvalidEntityModelException final : public Exception
{
public:
	InvalidEntityModelException()
		: Exception("Invalid Entity Model")
	{
	}
};

/* ************************************************************************** */
/* CapabilityDelegate life cycle                                              */
/* ************************************************************************** */
// clang-format off
CapabilityDelegate::CapabilityDelegate(protocol::ProtocolInterface* const protocolInterface, Entity const& entity, model::EntityTree const* const entityModelTree)
try
	: _protocolInterface{ protocolInterface }
	, _entityID{ entity.getEntityID() }
	, _listenerMac{ listenerMacFromEntity(entity) }
	, _entityModelTree{ entityModelTree }
	, _aemHandler{ entity, entityModelTree, {}, {}, {}, takeListenerCountersProvider(entity.getEntityID()) }
	, _bindObserver{ takeListenerBindObserver(entity.getEntityID()) }
{
	// 3SB #226: register as the live delegate for this entity so the daemon can push unsolicited
	// STREAM_INPUT counters notifications at runtime (removed in the dtor).
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	liveListenerDelegateRegistry()[_entityID.getValue()] = this;
}
catch (Exception const&)
{
	throw InvalidEntityModelException();
}
// clang-format on

CapabilityDelegate::~CapabilityDelegate() noexcept
{
	auto const lock = std::lock_guard{ listenerRegistryMutex() };
	auto& registry = liveListenerDelegateRegistry();
	auto const it = registry.find(_entityID.getValue());
	if (it != registry.end() && it->second == this)
	{
		registry.erase(it);
	}
}

void CapabilityDelegate::pushStreamInputCountersNotification(std::uint16_t const streamIndex, model::DescriptorCounterValidFlag const validCounters, model::DescriptorCounters const& counters) noexcept
{
	try
	{
		auto ser = protocol::aemPayload::serializeGetCountersResponse(model::DescriptorType::StreamInput, static_cast<model::DescriptorIndex>(streamIndex), validCounters, counters);
		pushUnsolicitedAemNotification(_protocolInterface, protocol::AemCommandType::GetCounters, UniqueIdentifier{}, ser.data(), ser.size());
	}
	catch (...)
	{
	}
}

/* ************************************************************************** */
/* CapabilityDelegate overrides — AECP (identical shape to the talker)        */
/* ************************************************************************** */
bool CapabilityDelegate::onUnhandledAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& aecpdu) noexcept
{
	if (aecpdu.getMessageType() == protocol::AecpMessageType::AemCommand)
	{
		auto const& aem = static_cast<protocol::AemAecpdu const&>(aecpdu);

		if (aem.getCommandType() == protocol::AemCommandType::ControllerAvailable)
		{
			LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, nullptr, 0u);
			return true;
		}
		if (aem.getCommandType() == protocol::AemCommandType::LockEntity)
		{
			handleLockEntity(pi, aem);
			return true;
		}
		if (aem.getCommandType() == protocol::AemCommandType::RegisterUnsolicitedNotification)
		{
			registerUnsolicited(aem.getControllerEntityID(), aem.getSrcAddress());
			LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, nullptr, 0u);
			return true;
		}
		if (aem.getCommandType() == protocol::AemCommandType::DeregisterUnsolicitedNotification)
		{
			deregisterUnsolicited(aem.getControllerEntityID());
			LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, nullptr, 0u);
			return true;
		}
		return _aemHandler.onUnhandledAecpAemCommand(pi, aem);
	}
	return false;
}

void CapabilityDelegate::handleLockEntity(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& aem) noexcept
{
	try
	{
		auto const [flags, lockedID, descriptorType, descriptorIndex] = protocol::aemPayload::deserializeLockEntityCommand(aem.getPayload());
		auto const controllerID = aem.getControllerEntityID();

		auto status = protocol::AemAecpStatus::Success;
		auto responseLockedID = UniqueIdentifier{};
		{
			std::lock_guard<std::mutex> const lock(_lockMutex);
			if (flags == protocol::AemLockEntityFlags::Unlock)
			{
				if (!_lockHolder || _lockHolder == controllerID)
				{
					_lockHolder = UniqueIdentifier{};
				}
				else
				{
					status = protocol::AemAecpStatus::EntityLocked;
					responseLockedID = _lockHolder;
				}
			}
			else // Lock
			{
				if (!_lockHolder || _lockHolder == controllerID)
				{
					_lockHolder = controllerID;
					responseLockedID = controllerID;
				}
				else
				{
					status = protocol::AemAecpStatus::EntityLocked;
					responseLockedID = _lockHolder;
				}
			}
		}

		auto ser = protocol::aemPayload::serializeLockEntityResponse(flags, responseLockedID, descriptorType, descriptorIndex);
		LocalEntityImpl<>::sendAemAecpResponse(pi, aem, status, ser.data(), ser.size());

		if (status == protocol::AemAecpStatus::Success)
		{
			pushUnsolicitedAemNotification(pi, protocol::AemCommandType::LockEntity, controllerID, ser.data(), ser.size());
		}
	}
	catch (...)
	{
		LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::BadArguments);
	}
}

/* ************************************************************************** */
/* Unsolicited notifications (identical to the talker delegate)               */
/* ************************************************************************** */
void CapabilityDelegate::registerUnsolicited(UniqueIdentifier const controllerID, networkInterface::MacAddress const& mac) noexcept
{
	if (!controllerID)
	{
		return;
	}
	std::lock_guard<std::mutex> const lock(_unsolicitedMutex);
	_unsolicitedSubscribers[controllerID].mac = mac;
}

void CapabilityDelegate::deregisterUnsolicited(UniqueIdentifier const controllerID) noexcept
{
	std::lock_guard<std::mutex> const lock(_unsolicitedMutex);
	_unsolicitedSubscribers.erase(controllerID);
}

void CapabilityDelegate::pushUnsolicitedAemNotification(protocol::ProtocolInterface* const pi, protocol::AemCommandType const commandType, UniqueIdentifier const excludeController, std::uint8_t const* const payload, size_t const payloadLength) noexcept
{
	std::lock_guard<std::mutex> const lock(_unsolicitedMutex);
	for (auto& [controllerID, subscriber] : _unsolicitedSubscribers)
	{
		if (controllerID == excludeController)
		{
			continue;
		}
		try
		{
			auto frame = protocol::AemAecpdu::create(true /* isResponse */);
			auto* const aem = static_cast<protocol::AemAecpdu*>(frame.get());
			aem->setSrcAddress(pi->getMacAddress());
			aem->setDestAddress(subscriber.mac);
			aem->setStatus(protocol::AemAecpStatus::Success);
			aem->setTargetEntityID(_entityID);
			aem->setControllerEntityID(controllerID);
			aem->setSequenceID(subscriber.nextSequenceID++);
			aem->setUnsolicited(true);
			aem->setCommandType(commandType);
			if (payload != nullptr && payloadLength != 0u)
			{
				aem->setCommandSpecificData(payload, payloadLength);
			}
			pi->sendAecpResponse(std::move(frame));
		}
		catch (...)
		{
		}
	}
}

/* ************************************************************************** */
/* Milan Vendor Unique (MVU)                                                  */
/* ************************************************************************** */
bool CapabilityDelegate::onUnhandledAecpVuCommand(protocol::ProtocolInterface* const pi, protocol::VuAecpdu::ProtocolIdentifier const& protocolIdentifier, protocol::Aecpdu const& aecpdu) noexcept
{
	if (!(protocolIdentifier == protocol::MvuAecpdu::ProtocolID))
	{
		return false;
	}
	auto const& mvu = static_cast<protocol::MvuAecpdu const&>(aecpdu);
	if (mvu.getCommandType() == protocol::MvuCommandType::GetMilanInfo)
	{
		sendMilanInfoResponse(pi, mvu);
		return true;
	}
	if (mvu.getCommandType() == protocol::MvuCommandType::GetMediaClockReferenceInfo)
	{
		return sendMediaClockReferenceInfoResponse(pi, mvu);
	}
	if (mvu.getCommandType() == protocol::MvuCommandType::GetStreamInputInfoEx)
	{
		return sendStreamInputInfoExResponse(pi, mvu);
	}
	return false;
}

void CapabilityDelegate::sendMilanInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept
{
	try
	{
		// 3SB listener Milan profile: Milan-compatible (protocolVersion 1). No talker/redundancy/MVU
		// binding features (None) — it is a plain Milan sink.
		auto info = model::MilanInfo{};
		info.protocolVersion = 1u;
		info.featuresFlags = entity::MilanInfoFeaturesFlags{};
		info.certificationVersion = model::MilanVersion{};
		info.specificationVersion = model::MilanVersion{ 1u, 3u };
		auto ser = protocol::mvuPayload::serializeGetMilanInfoResponse(info);

		auto frame = protocol::MvuAecpdu::create(true /* isResponse */);
		auto* const mvu = static_cast<protocol::MvuAecpdu*>(frame.get());
		mvu->setSrcAddress(pi->getMacAddress());
		mvu->setDestAddress(command.getSrcAddress());
		mvu->setStatus(protocol::AecpStatus::Success);
		mvu->setTargetEntityID(command.getTargetEntityID());
		mvu->setControllerEntityID(command.getControllerEntityID());
		mvu->setSequenceID(command.getSequenceID());
		mvu->setUnsolicited(false);
		mvu->setCommandType(protocol::MvuCommandType::GetMilanInfo);
		mvu->setCommandSpecificData(ser.data(), ser.size());

		pi->sendAecpResponse(std::move(frame));
	}
	catch (...)
	{
	}
}

bool CapabilityDelegate::sendMediaClockReferenceInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept
{
	try
	{
		// The 3SB listener exposes a single clock domain (index 0), recovered from the bound stream.
		auto const [clockDomainIndex] = protocol::mvuPayload::deserializeGetMediaClockReferenceInfoCommand(command.getPayload());
		if (clockDomainIndex != model::ClockDomainIndex{ 0u })
		{
			return false;
		}

		auto const flags = entity::MediaClockReferenceInfoFlags{};
		auto const defaultMcrPrio = model::DefaultMediaClockReferencePriority::Default;
		auto const userMcrPrio = model::MediaClockReferencePriority{ 0u };
		auto const domainName = model::AvdeccFixedString{};
		auto ser = protocol::mvuPayload::serializeGetMediaClockReferenceInfoResponse(clockDomainIndex, flags, defaultMcrPrio, userMcrPrio, domainName);

		auto frame = protocol::MvuAecpdu::create(true /* isResponse */);
		auto* const mvu = static_cast<protocol::MvuAecpdu*>(frame.get());
		mvu->setSrcAddress(pi->getMacAddress());
		mvu->setDestAddress(command.getSrcAddress());
		mvu->setStatus(protocol::AecpStatus::Success);
		mvu->setTargetEntityID(command.getTargetEntityID());
		mvu->setControllerEntityID(command.getControllerEntityID());
		mvu->setSequenceID(command.getSequenceID());
		mvu->setUnsolicited(false);
		mvu->setCommandType(protocol::MvuCommandType::GetMediaClockReferenceInfo);
		mvu->setCommandSpecificData(ser.data(), ser.size());

		pi->sendAecpResponse(std::move(frame));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool CapabilityDelegate::sendStreamInputInfoExResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept
{
	try
	{
		auto const [descriptorType, descriptorIndex] = protocol::mvuPayload::deserializeGetStreamInputInfoExCommand(command.getPayload());
		if (descriptorType != model::DescriptorType::StreamInput || descriptorIndex >= model::StreamIndex{ streamInputCount() })
		{
			return false;
		}

		// 3SB #226: report probingStatus = Completed. Controllers (Hive) query GetStreamInputInfoEx ONLY
		// once at enumeration (not on connection change) and gate the "Media Locked" display on
		// probingStatus == Completed. The default Disabled would fail that gate forever — even after the
		// stream locks — so we report Completed (the sink settles via a single CONNECT_TX, not Milan
		// fast-connect probing) and let the live MEDIA_LOCKED stream-input counters (pushed unsolicited
		// on lock change) drive the actual lock display. Populate the bound talker when connected.
		auto info = model::StreamInputInfoEx{};
		info.probingStatus = model::ProbingStatus::Completed;
		info.acmpStatus = protocol::AcmpStatus::Success;
		{
			std::lock_guard<std::mutex> const lock(_bindingsMutex);
			auto const it = _bindings.find(static_cast<protocol::AcmpUniqueID>(descriptorIndex));
			if (it != _bindings.end() && it->second.connected)
			{
				info.talkerStream = model::StreamIdentification{ it->second.talkerEntityID, static_cast<model::StreamIndex>(it->second.talkerUniqueID) };
			}
		}
		auto ser = protocol::mvuPayload::serializeGetStreamInputInfoExResponse(descriptorType, descriptorIndex, info);

		auto frame = protocol::MvuAecpdu::create(true /* isResponse */);
		auto* const mvu = static_cast<protocol::MvuAecpdu*>(frame.get());
		mvu->setSrcAddress(pi->getMacAddress());
		mvu->setDestAddress(command.getSrcAddress());
		mvu->setStatus(protocol::AecpStatus::Success);
		mvu->setTargetEntityID(command.getTargetEntityID());
		mvu->setControllerEntityID(command.getControllerEntityID());
		mvu->setSequenceID(command.getSequenceID());
		mvu->setUnsolicited(false);
		mvu->setCommandType(protocol::MvuCommandType::GetStreamInputInfoEx);
		mvu->setCommandSpecificData(ser.data(), ser.size());

		pi->sendAecpResponse(std::move(frame));
		return true;
	}
	catch (...)
	{
		return false;
	}
}

/* ************************************************************************** */
/* ACMP listener state machine (software-mode P2)                             */
/* ************************************************************************** */
std::uint16_t CapabilityDelegate::streamInputCount() const noexcept
{
	if (_entityModelTree == nullptr)
	{
		return 0u;
	}
	auto const configIndex = _entityModelTree->dynamicModel.currentConfiguration;
	auto const it = _entityModelTree->configurationTrees.find(configIndex);
	if (it == _entityModelTree->configurationTrees.end())
	{
		return 0u;
	}
	return static_cast<std::uint16_t>(it->second.streamInputModels.size());
}

networkInterface::MacAddress CapabilityDelegate::listenerMacFromEntity(Entity const& entity) noexcept
{
	auto const& interfaces = entity.getInterfacesInformation();
	if (!interfaces.empty())
	{
		return interfaces.begin()->second.macAddress;
	}
	return networkInterface::MacAddress{};
}

void CapabilityDelegate::sendListenerResponse(protocol::AcmpMessageType const responseType, protocol::AcmpStatus const status, ControllerRequest const& req, std::uint64_t const streamID, networkInterface::MacAddress const& streamDestAddress, std::uint16_t const streamVlanID, std::uint16_t const connectionCount) const noexcept
{
	auto responseUP = protocol::Acmpdu::create();
	auto& response = *responseUP;

	response.setMessageType(responseType);
	response.setStatus(status);
	response.setStreamID(streamID);
	response.setControllerEntityID(req.controllerID);
	response.setTalkerEntityID(req.talkerEntityID);
	response.setListenerEntityID(_entityID);
	response.setTalkerUniqueID(req.talkerUniqueID);
	response.setListenerUniqueID(req.listenerUniqueID);
	response.setStreamDestAddress(streamDestAddress);
	response.setConnectionCount(connectionCount);
	response.setSequenceID(req.sequenceID);
	response.setFlags(req.flags);
	response.setStreamVlanID(streamVlanID);

	_protocolInterface->sendAcmpResponse(std::move(responseUP));
}

void CapabilityDelegate::initiateTalkerHandshake(protocol::ProtocolInterface* const pi, ControllerRequest const& req, bool const connect) noexcept
{
	auto const responseType = connect ? protocol::AcmpMessageType::ConnectRxResponse : protocol::AcmpMessageType::DisconnectRxResponse;

	// Stage 1: ask the talker. The CommandStateMachine assigns the sequence id, handles retry/timeout,
	// and delivers the talker's response (or an error) to our handler. We capture req by value because
	// this is asynchronous. Capturing `this` requires the delegate to outlive the in-flight command —
	// the same lifetime contract the controller/talker delegates rely on (the SM is torn down with us).
	//
	// The controller_entity_id of this CONNECT_TX is OUR listener entity id (_entityID), NOT the
	// original controller (req.controllerID). IEEE 1722.1 would carry the original controller, but
	// la_avdecc's CommandStateMachine only sends ACMP commands whose controller_entity_id is a
	// REGISTERED LOCAL entity (commandStateMachine.cpp: _commandEntities.find(controllerEntityID) ->
	// InvalidEntityType otherwise) — a remote controller id is rejected and the frame never hits the
	// wire. The listener acts as its own controller for the talker handshake; the field is
	// informational to the talker, which sets up the stream regardless. The result is forwarded back
	// to the real controller via the CONNECT_RX_RESPONSE (sendListenerResponse), which already carries
	// req.controllerID in its ACMP fields.
	LocalEntityImpl<>::sendAcmpCommand(pi, connect ? protocol::AcmpMessageType::ConnectTxCommand : protocol::AcmpMessageType::DisconnectTxCommand, _entityID, req.talkerEntityID, static_cast<model::StreamIndex>(req.talkerUniqueID), _entityID, static_cast<model::StreamIndex>(req.listenerUniqueID), std::uint16_t{ 0u },
		[this, req, connect, responseType](protocol::Acmpdu const* const response, LocalEntity::ControlStatus const status) noexcept
		{
			// Talker unreachable / timed out: tell the controller the listener-talker timed out.
			if (status != LocalEntity::ControlStatus::Success || response == nullptr)
			{
				sendListenerResponse(responseType, protocol::AcmpStatus::ListenerTalkerTimeout, req, 0u, networkInterface::MacAddress{}, 0u, 0u);
				return;
			}

			auto const talkerStatus = response->getStatus();
			auto const streamID = response->getStreamID();
			auto const destMac = response->getStreamDestAddress();
			auto const vlanID = response->getStreamVlanID();

			// Stage 2: bind/unbind and answer the controller.
			std::uint16_t connectionCount = 0u;
			if (talkerStatus == protocol::AcmpStatus::Success)
			{
				std::lock_guard<std::mutex> const lock(_bindingsMutex);
				auto& binding = _bindings[req.listenerUniqueID];
				if (connect)
				{
					binding.connected = true;
					binding.talkerEntityID = req.talkerEntityID;
					binding.talkerUniqueID = req.talkerUniqueID;
					binding.streamID = streamID;
					binding.streamDestAddress = destMac;
					binding.vlanID = vlanID;
					binding.flags = req.flags;
					connectionCount = 1u;
				}
				else
				{
					binding.connected = false;
					connectionCount = 0u;
				}
			}

			sendListenerResponse(responseType, talkerStatus, req, streamID, destMac, vlanID, connectionCount);

			// Drive the data plane (point the AAF receiver at / away from the bound stream).
			// destMac/vlanID are the talker-reported wire identifiers the receiver binds its
			// SOCK_RAW socket and stream filter to (alongside streamID).
			if (talkerStatus == protocol::AcmpStatus::Success && _bindObserver)
			{
				_bindObserver(req.listenerUniqueID, connect, streamID, destMac, vlanID);
			}
		});
}

void CapabilityDelegate::onAcmpCommand(protocol::ProtocolInterface* const pi, protocol::Acmpdu const& acmpdu) noexcept
{
	// Only respond to listener-side commands addressed to our entity.
	if (acmpdu.getListenerEntityID() != _entityID)
	{
		return;
	}
	auto const messageType = acmpdu.getMessageType();
	if (messageType != protocol::AcmpMessageType::ConnectRxCommand && messageType != protocol::AcmpMessageType::DisconnectRxCommand && messageType != protocol::AcmpMessageType::GetRxStateCommand)
	{
		return;
	}

	auto req = ControllerRequest{};
	req.controllerID = acmpdu.getControllerEntityID();
	req.talkerEntityID = acmpdu.getTalkerEntityID();
	req.talkerUniqueID = acmpdu.getTalkerUniqueID();
	req.listenerUniqueID = acmpdu.getListenerUniqueID();
	req.sequenceID = acmpdu.getSequenceID();
	req.flags = acmpdu.getFlags();

	auto const responseType = (messageType == protocol::AcmpMessageType::ConnectRxCommand) ? protocol::AcmpMessageType::ConnectRxResponse : (messageType == protocol::AcmpMessageType::DisconnectRxCommand) ? protocol::AcmpMessageType::DisconnectRxResponse : protocol::AcmpMessageType::GetRxStateResponse;

	// Validate the listener stream index.
	if (req.listenerUniqueID >= streamInputCount())
	{
		sendListenerResponse(responseType, protocol::AcmpStatus::ListenerUnknownID, req, 0u, networkInterface::MacAddress{}, 0u, 0u);
		return;
	}

	if (messageType == protocol::AcmpMessageType::GetRxStateCommand)
	{
		// Synchronous: report the current binding for this STREAM_INPUT.
		std::lock_guard<std::mutex> const lock(_bindingsMutex);
		auto const it = _bindings.find(req.listenerUniqueID);
		if (it != _bindings.end() && it->second.connected)
		{
			auto const& b = it->second;
			// Reflect the bound talker in the response (GET_RX_STATE carries the connected talker).
			auto stateReq = req;
			stateReq.talkerEntityID = b.talkerEntityID;
			stateReq.talkerUniqueID = b.talkerUniqueID;
			sendListenerResponse(responseType, protocol::AcmpStatus::Success, stateReq, b.streamID, b.streamDestAddress, b.vlanID, std::uint16_t{ 1u });
		}
		else
		{
			sendListenerResponse(responseType, protocol::AcmpStatus::NotConnected, req, 0u, networkInterface::MacAddress{}, 0u, 0u);
		}
		return;
	}

	// CONNECT_RX / DISCONNECT_RX: run the two-stage talker handshake.
	initiateTalkerHandshake(pi, req, messageType == protocol::AcmpMessageType::ConnectRxCommand);
}

} // namespace listener
} // namespace entity
} // namespace avdecc
} // namespace la
