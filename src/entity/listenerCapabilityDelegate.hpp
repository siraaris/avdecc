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
* @file listenerCapabilityDelegate.hpp
* @author Christophe Calmejane
*
* @brief Listener entity-responder capability delegate.
*
* 3SB note: la_avdecc upstream ships the listener CapabilityDelegate as a TODO
* stub (aggregateEntityImpl.cpp asserts-false). This additive implementation
* mirrors talkerCapabilityDelegate and provides the entity-responder side of a
* Milan listener (the 3SB software-mode listener role, GH software-mode P2):
*  - ADP advertise + AECP AEM responder (READ_DESCRIPTOR via the shared AemHandler),
*    LOCK_ENTITY, unsolicited notifications — identical to the talker delegate.
*  - MVU GET_MILAN_INFO (listener sink signalling) + Milan 1.3 dynamic info for the
*    STREAM_INPUT descriptors.
*  - The ACMP listener state machine (onAcmpCommand): CONNECT_RX / DISCONNECT_RX /
*    GET_RX_STATE. CONNECT_RX drives the two-stage handshake — the listener issues a
*    CONNECT_TX_COMMAND to the talker (via ProtocolInterface::sendAcmpCommand, whose
*    CommandStateMachine handles retry/timeout/correlation) and, on the talker's
*    response, binds the stream input and answers the controller.
*
* The data-plane gate (telling the AAF receiver which talker stream / dest-mac /
* vlan to receive) is surfaced via the ListenerBindObserver registry, mirroring the
* talker's TalkerConnectionObserver. On-wire verification (a controller issuing
* CONNECT_RX, audio flowing into the receiver) is a rig checkpoint.
*/

#pragma once

#include "la/avdecc/internals/entityModelTree.hpp"
#include "la/avdecc/internals/protocolAcmpdu.hpp"
#include "la/avdecc/internals/protocolMvuAecpdu.hpp"

#include "entityImpl.hpp"
#include "aemHandler.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace la
{
namespace avdecc
{
namespace entity
{
namespace listener
{
class CapabilityDelegate final : public entity::CapabilityDelegate
{
public:
	/* ************************************************************************** */
	/* CapabilityDelegate life cycle                                              */
	/* ************************************************************************** */
	CapabilityDelegate(protocol::ProtocolInterface* const protocolInterface, Entity const& entity, model::EntityTree const* const entityModelTree);
	virtual ~CapabilityDelegate() noexcept;

	// Deleted compiler auto-generated methods
	CapabilityDelegate(CapabilityDelegate&&) = delete;
	CapabilityDelegate(CapabilityDelegate const&) = delete;
	CapabilityDelegate& operator=(CapabilityDelegate const&) = delete;
	CapabilityDelegate& operator=(CapabilityDelegate&&) = delete;

private:
	/* ************************************************************************** */
	/* CapabilityDelegate overrides                                               */
	/* ************************************************************************** */
	/* **** AECP notifications **** */
	virtual bool onUnhandledAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& aecpdu) noexcept override;
	virtual bool onUnhandledAecpVuCommand(protocol::ProtocolInterface* const pi, protocol::VuAecpdu::ProtocolIdentifier const& protocolIdentifier, protocol::Aecpdu const& aecpdu) noexcept override;
	/* **** ACMP notifications **** */
	virtual void onAcmpCommand(protocol::ProtocolInterface* const pi, protocol::Acmpdu const& acmpdu) noexcept override;

	/* ************************************************************************** */
	/* AECP helpers (shared shape with the talker delegate)                       */
	/* ************************************************************************** */
	std::uint16_t streamInputCount() const noexcept;
	void handleLockEntity(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& aem) noexcept;
	void sendMilanInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	bool sendMediaClockReferenceInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	bool sendStreamInputInfoExResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	static networkInterface::MacAddress listenerMacFromEntity(Entity const& entity) noexcept;

	/* ************************************************************************** */
	/* ACMP listener state machine helpers                                        */
	/* ************************************************************************** */
	// Captured controller-command fields (the async talker round-trip outlives the notification frame,
	// so CONNECT_RX/DISCONNECT_RX copy what they need rather than hold the Acmpdu reference).
	struct ControllerRequest
	{
		UniqueIdentifier controllerID{};
		UniqueIdentifier talkerEntityID{};
		protocol::AcmpUniqueID talkerUniqueID{ 0u };
		protocol::AcmpUniqueID listenerUniqueID{ 0u };
		protocol::AcmpSequenceID sequenceID{ 0u };
		entity::ConnectionFlags flags{};
	};
	// Send a listener-side response (CONNECT_RX / DISCONNECT_RX / GET_RX_STATE) to the controller.
	void sendListenerResponse(protocol::AcmpMessageType const responseType, protocol::AcmpStatus const status, ControllerRequest const& req, std::uint64_t const streamID, networkInterface::MacAddress const& streamDestAddress, std::uint16_t const streamVlanID, std::uint16_t const connectionCount) const noexcept;
	// Stage 1 of CONNECT_RX / DISCONNECT_RX: ask the talker (CONNECT_TX / DISCONNECT_TX) and, on its
	// response, bind/unbind the input and answer the controller (stage 2).
	void initiateTalkerHandshake(protocol::ProtocolInterface* const pi, ControllerRequest const& req, bool const connect) noexcept;

	/* ************************************************************************** */
	/* Internal variables                                                         */
	/* ************************************************************************** */
	protocol::ProtocolInterface* const _protocolInterface{ nullptr };
	UniqueIdentifier const _entityID{ UniqueIdentifier::getNullUniqueIdentifier() };
	networkInterface::MacAddress const _listenerMac{};
	model::EntityTree const* const _entityModelTree{ nullptr };
	model::AemHandler const _aemHandler;
	// Invoked on CONNECT_RX/DISCONNECT_RX with the bound talker stream so the data plane (AAF
	// receiver) knows which stream_id / dest_mac / vlan to receive. Runs on the protocol-interface
	// thread. See aggregateEntity.hpp (ListenerBindObserver).
	std::function<void(std::uint16_t /*listenerStreamIndex*/, bool /*bound*/, std::uint64_t /*streamID*/, networkInterface::MacAddress const& /*destMac*/, std::uint16_t /*vlanID*/)> const _bindObserver;

	// Per-STREAM_INPUT-descriptor binding state (the ACMP listener "RX state").
	struct Binding
	{
		bool connected{ false };
		UniqueIdentifier talkerEntityID{};
		protocol::AcmpUniqueID talkerUniqueID{ 0u };
		std::uint64_t streamID{ 0u };
		networkInterface::MacAddress streamDestAddress{};
		entity::ConnectionFlags flags{};
	};
	mutable std::mutex _bindingsMutex;
	std::unordered_map<protocol::AcmpUniqueID, Binding> _bindings;

	// LOCK_ENTITY state (mandatory for Milan), identical to the talker delegate.
	mutable std::mutex _lockMutex;
	UniqueIdentifier _lockHolder{};

	// Unsolicited subscribers (GH #15 / #169), identical to the talker delegate.
	struct UnsolicitedSubscriber
	{
		networkInterface::MacAddress mac{};
		std::uint16_t nextSequenceID{ 0u };
	};
	mutable std::mutex _unsolicitedMutex;
	std::unordered_map<UniqueIdentifier, UnsolicitedSubscriber, UniqueIdentifier::hash> _unsolicitedSubscribers;

	void registerUnsolicited(UniqueIdentifier const controllerID, networkInterface::MacAddress const& mac) noexcept;
	void deregisterUnsolicited(UniqueIdentifier const controllerID) noexcept;
	void pushUnsolicitedAemNotification(protocol::ProtocolInterface* const pi, protocol::AemCommandType const commandType, UniqueIdentifier const excludeController, std::uint8_t const* const payload, size_t const payloadLength) noexcept;
};

} // namespace listener
} // namespace entity
} // namespace avdecc
} // namespace la
