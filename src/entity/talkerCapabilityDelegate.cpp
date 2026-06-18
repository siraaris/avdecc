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
* @file talkerCapabilityDelegate.cpp
* @author Christophe Calmejane
*
* @brief Talker entity-responder capability delegate (3SB additive milestone M1).
*        See talkerCapabilityDelegate.hpp for scope notes.
*/

#include "la/avdecc/utils.hpp"
#include "la/avdecc/internals/aggregateEntity.hpp" // setTalkerStreamOutputWireUids declaration (LA_AVDECC_API export)

#include "talkerCapabilityDelegate.hpp"
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
/* Talker STREAM_OUTPUT wire-uid registry (GH #15 / M5)                       */
/* ************************************************************************** */
// Side-channel from the daemon (which knows the data-plane stream layout) to the talker
// CapabilityDelegate (constructed internally by AggregateEntity). Keyed by entityID, written
// before create() and taken once at construction. See aggregateEntity.hpp for rationale.
namespace
{
std::mutex& wireUidRegistryMutex() noexcept
{
	static std::mutex s_mutex;
	return s_mutex;
}
// Keyed by the raw EntityID value (std::hash<UniqueIdentifier> is not provided).
std::unordered_map<UniqueIdentifier::value_type, std::vector<std::uint16_t>>& wireUidRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, std::vector<std::uint16_t>> s_registry;
	return s_registry;
}
// Per-STREAM_OUTPUT presentation time offset (ns). Set by the daemon before construction; consumed
// by AemHandler for GET_STREAM_INFO msrp_accumulated_latency + GET_MAX_TRANSIT_TIME. Same registry
// pattern + lifetime as the wire-uid map (the descriptor dynamic model is cleared by la_avdecc, so
// this value must live on the handler, not the tree).
std::unordered_map<UniqueIdentifier::value_type, std::vector<std::uint32_t>>& presentationOffsetRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, std::vector<std::uint32_t>> s_registry;
	return s_registry;
}
} // namespace

void LA_AVDECC_CALL_CONVENTION setTalkerStreamOutputWireUids(UniqueIdentifier const entityID, std::vector<std::uint16_t> const& wireUids) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	wireUidRegistry()[entityID.getValue()] = wireUids;
}

/* ************************************************************************** */
/* Talker ACMP connection observer registry (GH #15 / M5)                     */
/* ************************************************************************** */
// Lets the daemon learn when a listener connects/disconnects from a talker stream (to drive the
// avtpd transmit gate). Same registry pattern + lifetime as the wire-uid map above.
namespace
{
std::unordered_map<UniqueIdentifier::value_type, TalkerConnectionObserver>& connectionObserverRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, TalkerConnectionObserver> s_registry;
	return s_registry;
}
} // namespace

void LA_AVDECC_CALL_CONVENTION setTalkerConnectionObserver(UniqueIdentifier const entityID, TalkerConnectionObserver observer) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	connectionObserverRegistry()[entityID.getValue()] = std::move(observer);
}

/* ************************************************************************** */
/* Talker GET_COUNTERS provider registry (GH #15 / M5)                        */
/* ************************************************************************** */
namespace
{
std::unordered_map<UniqueIdentifier::value_type, TalkerCountersProvider>& countersProviderRegistry() noexcept
{
	static std::unordered_map<UniqueIdentifier::value_type, TalkerCountersProvider> s_registry;
	return s_registry;
}
} // namespace

void LA_AVDECC_CALL_CONVENTION setTalkerCountersProvider(UniqueIdentifier const entityID, TalkerCountersProvider provider) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	countersProviderRegistry()[entityID.getValue()] = std::move(provider);
}

void LA_AVDECC_CALL_CONVENTION setTalkerStreamOutputPresentationOffsetsNs(UniqueIdentifier const entityID, std::vector<std::uint32_t> const& offsetsNs) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	presentationOffsetRegistry()[entityID.getValue()] = offsetsNs;
}

namespace talker
{
namespace
{
// Take (read + erase) the registered wire-uid mapping for an entity, or empty if none.
std::vector<std::uint16_t> takeStreamOutputWireUids(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	auto& registry = wireUidRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto uids = std::move(it->second);
	registry.erase(it);
	return uids;
}

// Take (read + erase) the registered connection observer for an entity, or empty if none.
TalkerConnectionObserver takeTalkerConnectionObserver(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	auto& registry = connectionObserverRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto observer = std::move(it->second);
	registry.erase(it);
	return observer;
}

// Take (read + erase) the registered counters provider for an entity, or empty if none.
TalkerCountersProvider takeTalkerCountersProvider(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	auto& registry = countersProviderRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto provider = std::move(it->second);
	registry.erase(it);
	return provider;
}

// Take (read + erase) the registered per-stream presentation time offsets for an entity, or empty.
std::vector<std::uint32_t> takeStreamOutputPresentationOffsetsNs(UniqueIdentifier const entityID) noexcept
{
	auto const lock = std::lock_guard{ wireUidRegistryMutex() };
	auto& registry = presentationOffsetRegistry();
	auto const it = registry.find(entityID.getValue());
	if (it == registry.end())
	{
		return {};
	}
	auto offsets = std::move(it->second);
	registry.erase(it);
	return offsets;
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
// Disabling formatting for this constructor as try-catching initializer is not properly supported
CapabilityDelegate::CapabilityDelegate(protocol::ProtocolInterface* const protocolInterface, Entity const& entity, model::EntityTree const* const entityModelTree)
try
	: _protocolInterface{ protocolInterface }
	, _entityID{ entity.getEntityID() }
	, _talkerMac{ talkerMacFromEntity(entity) }
	, _entityModelTree{ entityModelTree }
	, _streamOutputWireUids{ takeStreamOutputWireUids(entity.getEntityID()) }
	, _aemHandler{ entity, entityModelTree, _streamOutputWireUids, takeTalkerCountersProvider(entity.getEntityID()), takeStreamOutputPresentationOffsetsNs(entity.getEntityID()) }
	, _connectionObserver{ takeTalkerConnectionObserver(entity.getEntityID()) }
{
}
catch (Exception const&)
{
	throw InvalidEntityModelException();
}
// clang-format on

CapabilityDelegate::~CapabilityDelegate() noexcept {}

/* ************************************************************************** */
/* CapabilityDelegate overrides                                               */
/* ************************************************************************** */
/* **** AECP notifications **** */
bool CapabilityDelegate::onUnhandledAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& aecpdu) noexcept
{
	if (aecpdu.getMessageType() == protocol::AecpMessageType::AemCommand)
	{
		auto const& aem = static_cast<protocol::AemAecpdu const&>(aecpdu);

		// Answer ControllerAvailable directly (we are reachable)
		if (aem.getCommandType() == protocol::AemCommandType::ControllerAvailable)
		{
			LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, nullptr, 0u);
			return true;
		}

		// LOCK_ENTITY is mandatory for Milan and stateful, so handle it here (the shared AemHandler
		// is const) rather than in the descriptor-read handler.
		if (aem.getCommandType() == protocol::AemCommandType::LockEntity)
		{
			handleLockEntity(pi, aem);
			return true;
		}

		// REGISTER/DEREGISTER_UNSOLICITED_NOTIFICATION: track the subscriber (the shared const
		// AemHandler can't), then ack. We push real unsolicited notifications to subscribers on
		// state changes (e.g. LOCK_ENTITY) — see pushUnsolicitedAemNotification. (GH #15 / #169.)
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

		// Delegate descriptor reads (and any other AemHandler-supported commands)
		// to the shared AemHandler, exactly as controller::CapabilityDelegate does.
		return _aemHandler.onUnhandledAecpAemCommand(pi, aem);
	}
	return false;
}

/* ************************************************************************** */
/* LOCK_ENTITY (mandatory for Milan)                                          */
/* ************************************************************************** */
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
				// Only the holder may unlock; a non-holder gets EntityLocked + the current holder.
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

		// On a successful lock/unlock, push an unsolicited LOCK_ENTITY notification to every other
		// subscribed controller so they learn the new lock holder without polling. (GH #15 / #169.)
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
/* Unsolicited notifications (GH #15 / #169)                                  */
/* ************************************************************************** */
void CapabilityDelegate::registerUnsolicited(UniqueIdentifier const controllerID, networkInterface::MacAddress const& mac) noexcept
{
	if (!controllerID)
	{
		return;
	}
	std::lock_guard<std::mutex> const lock(_unsolicitedMutex);
	// operator[] preserves an existing subscriber's sequence-id counter on re-registration.
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
			continue; // the initiating controller already received the solicited response
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
/* Milan Vendor Unique (MVU) — GET_MILAN_INFO (GH #15 / M4)                    */
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
	// Milan 1.3 mandatory dynamic info (§5.4.2): answering these keeps the controller at Milan
	// 1.3 — a NotImplemented response makes it auto-downgrade us to 1.2. (GH #15 / #167.)
	if (mvu.getCommandType() == protocol::MvuCommandType::GetMediaClockReferenceInfo)
	{
		return sendMediaClockReferenceInfoResponse(pi, mvu);
	}
	if (mvu.getCommandType() == protocol::MvuCommandType::GetStreamInputInfoEx)
	{
		return sendStreamInputInfoExResponse(pi, mvu);
	}
	// Other MVU commands (system unique id, stream binding) are not implemented; returning false
	// makes the local entity reflect a NotImplemented MVU response.
	return false;
}

void CapabilityDelegate::sendMilanInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept
{
	try
	{
		// 3SB talker Milan profile: Milan-compatible (protocolVersion 1), signals channel presence.
		auto info = model::MilanInfo{};
		info.protocolVersion = 1u;
		info.featuresFlags = entity::MilanInfoFeaturesFlags{ entity::MilanInfoFeaturesFlag::TalkerSignalPresence };
		info.certificationVersion = model::MilanVersion{}; // 0 — uncertified
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
		// The 3SB talker exposes exactly one clock domain (index 0, the CRF media clock).
		auto const [clockDomainIndex] = protocol::mvuPayload::deserializeGetMediaClockReferenceInfoCommand(command.getPayload());
		if (clockDomainIndex != model::ClockDomainIndex{ 0u })
		{
			return false; // unknown clock domain -> reflect NotImplemented
		}

		// Minimal valid Milan 1.3 report: default reference priority, no user override, no domain
		// name (flags clear). Enough to satisfy the controller's mandatory-dynamic-info check.
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
		// The 3SB talker exposes a single STREAM_INPUT (index 0, the CRF media-clock input).
		auto const [descriptorType, descriptorIndex] = protocol::mvuPayload::deserializeGetStreamInputInfoExCommand(command.getPayload());
		if (descriptorType != model::DescriptorType::StreamInput || descriptorIndex != model::StreamIndex{ 0u })
		{
			return false; // unknown stream input -> reflect NotImplemented
		}

		// The CRF input is not an ACMP listener (no fast-connect / probing), so report the
		// not-bound state: no talker stream, probing Disabled, ACMP Success (struct defaults).
		auto const info = model::StreamInputInfoEx{};
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
/* ACMP talker state machine (GH #15 / M3)                                    */
/* ************************************************************************** */
std::uint16_t CapabilityDelegate::streamOutputCount() const noexcept
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
	return static_cast<std::uint16_t>(it->second.streamOutputModels.size());
}

networkInterface::MacAddress CapabilityDelegate::talkerMacFromEntity(Entity const& entity) noexcept
{
	auto const& interfaces = entity.getInterfacesInformation();
	if (!interfaces.empty())
	{
		return interfaces.begin()->second.macAddress;
	}
	return networkInterface::MacAddress{};
}

std::uint16_t CapabilityDelegate::wireUidFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept
{
	if (talkerUniqueID < _streamOutputWireUids.size())
	{
		return _streamOutputWireUids[talkerUniqueID];
	}
	return static_cast<std::uint16_t>(talkerUniqueID);
}

std::uint64_t CapabilityDelegate::streamIdFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept
{
	// Standard AVTP stream_id = talker MAC (48 bits) << 16 | on-wire stream uid. This is what
	// avtpd transmits with, so a listener that connects via this response will receive. The wire
	// uid differs from the descriptor index for streams the data plane numbers separately (CRF).
	std::uint64_t macU48 = 0u;
	for (auto const octet : _talkerMac)
	{
		macU48 = (macU48 << 8) | static_cast<std::uint64_t>(octet);
	}
	return (macU48 << 16) | static_cast<std::uint64_t>(wireUidFor(talkerUniqueID));
}

networkInterface::MacAddress CapabilityDelegate::streamDestMacFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept
{
	// avtpd static-MAAP dest_mac = base 91:e0:f0:00:fe:00 + on-wire stream uid (== descriptor index
	// for AAF, but the data-plane CRF uid for the media-clock stream).
	return networkInterface::MacAddress{ { 0x91, 0xe0, 0xf0, 0x00, 0xfe, static_cast<std::uint8_t>(wireUidFor(talkerUniqueID) & 0xFFu) } };
}

void CapabilityDelegate::sendTalkerResponse(protocol::ProtocolInterface* const /*pi*/, protocol::Acmpdu const& command, protocol::AcmpMessageType const responseType, protocol::AcmpStatus const status, UniqueIdentifier const listenerEntityID, protocol::AcmpUniqueID const listenerUniqueID, std::uint16_t const connectionCount) const noexcept
{
	auto responseUP = protocol::Acmpdu::create();
	auto& response = *responseUP;
	auto const talkerUniqueID = command.getTalkerUniqueID();

	response.setMessageType(responseType);
	response.setStatus(status);
	response.setStreamID(streamIdFor(talkerUniqueID));
	response.setControllerEntityID(command.getControllerEntityID());
	response.setTalkerEntityID(_entityID);
	response.setListenerEntityID(listenerEntityID);
	response.setTalkerUniqueID(talkerUniqueID);
	response.setListenerUniqueID(listenerUniqueID);
	response.setStreamDestAddress(streamDestMacFor(talkerUniqueID));
	response.setConnectionCount(connectionCount);
	response.setSequenceID(command.getSequenceID());
	response.setFlags(command.getFlags());
	response.setStreamVlanID(std::uint16_t{ 2u }); // SR class A (avtpd default)

	_protocolInterface->sendAcmpResponse(std::move(responseUP));
}

void CapabilityDelegate::onAcmpCommand(protocol::ProtocolInterface* const pi, protocol::Acmpdu const& acmpdu) noexcept
{
	// Only respond to talker-side commands addressed to our entity.
	if (acmpdu.getTalkerEntityID() != _entityID)
	{
		return;
	}
	auto const messageType = acmpdu.getMessageType();
	if (messageType != protocol::AcmpMessageType::ConnectTxCommand && messageType != protocol::AcmpMessageType::DisconnectTxCommand && messageType != protocol::AcmpMessageType::GetTxStateCommand && messageType != protocol::AcmpMessageType::GetTxConnectionCommand)
	{
		return;
	}

	auto const responseType = (messageType == protocol::AcmpMessageType::ConnectTxCommand) ? protocol::AcmpMessageType::ConnectTxResponse : (messageType == protocol::AcmpMessageType::DisconnectTxCommand) ? protocol::AcmpMessageType::DisconnectTxResponse : (messageType == protocol::AcmpMessageType::GetTxStateCommand) ? protocol::AcmpMessageType::GetTxStateResponse : protocol::AcmpMessageType::GetTxConnectionResponse;

	auto const talkerUniqueID = acmpdu.getTalkerUniqueID();
	auto const listenerEntityID = acmpdu.getListenerEntityID();
	auto const listenerUniqueID = acmpdu.getListenerUniqueID();

	// Validate the talker stream index.
	if (talkerUniqueID >= streamOutputCount())
	{
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::TalkerNoStreamIndex, listenerEntityID, listenerUniqueID, 0u);
		return;
	}

	std::lock_guard<std::mutex> const lock(_connectionsMutex);
	auto& listeners = _connections[talkerUniqueID];

	if (messageType == protocol::AcmpMessageType::ConnectTxCommand)
	{
		auto const found = std::find_if(listeners.begin(), listeners.end(), [&](ListenerPair const& p) { return p.entityID == listenerEntityID && p.uniqueID == listenerUniqueID; });
		if (found == listeners.end())
		{
			listeners.push_back(ListenerPair{ listenerEntityID, listenerUniqueID });
		}
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, listenerEntityID, listenerUniqueID, static_cast<std::uint16_t>(listeners.size()));
		if (_connectionObserver)
		{
			_connectionObserver(talkerUniqueID, static_cast<std::uint16_t>(listeners.size()));
		}
	}
	else if (messageType == protocol::AcmpMessageType::DisconnectTxCommand)
	{
		listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [&](ListenerPair const& p) { return p.entityID == listenerEntityID && p.uniqueID == listenerUniqueID; }), listeners.end());
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, listenerEntityID, listenerUniqueID, static_cast<std::uint16_t>(listeners.size()));
		if (_connectionObserver)
		{
			_connectionObserver(talkerUniqueID, static_cast<std::uint16_t>(listeners.size()));
		}
	}
	else if (messageType == protocol::AcmpMessageType::GetTxStateCommand)
	{
		// Milan 1.3 §5.5.4.3: a Milan talker MUST report connection_count = 0 in
		// GET_TX_STATE_RESPONSE (the actual connections are enumerated via GET_TX_CONNECTION).
		// Reporting listeners.size() here passed M4 verification only because no listener was
		// connected at the time; once one connects, the nonzero count makes Hive flag the
		// talker non-Milan-compliant.
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, listenerEntityID, listenerUniqueID, 0u);
	}
	else // GetTxConnectionCommand: connection_count in the command carries the requested index.
	{
		auto const index = acmpdu.getConnectionCount();
		if (index < listeners.size())
		{
			auto const& pair = listeners[index];
			sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, pair.entityID, pair.uniqueID, static_cast<std::uint16_t>(listeners.size()));
		}
		else
		{
			sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::NoSuchConnection, listenerEntityID, listenerUniqueID, static_cast<std::uint16_t>(listeners.size()));
		}
	}
}

} // namespace talker
} // namespace entity
} // namespace avdecc
} // namespace la
