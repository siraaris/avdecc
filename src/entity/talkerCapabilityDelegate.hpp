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
* @file talkerCapabilityDelegate.hpp
* @author Christophe Calmejane
*
* @brief Talker entity-responder capability delegate.
*
* 3SB note: la_avdecc upstream (<= v4.3.1, dev as of 2026-04) ships the talker
* CapabilityDelegate as a TODO stub (aggregateEntityImpl.cpp asserts-false). This
* additive implementation provides the entity-responder side:
*  - M1: ADP advertise + AECP AEM responder (READ_DESCRIPTOR via the shared AemHandler).
*  - M2: a subset of dynamic AEM commands (handled in the shared AemHandler).
*  - M3: the ACMP talker state machine (onAcmpCommand) — CONNECT_TX / DISCONNECT_TX /
*    GET_TX_STATE / GET_TX_CONNECTION with per-stream connection tracking.
* The avtpd data-plane gate + real stream_id/dest_mac/vlan wiring is a later milestone
* (M5); M3 derives placeholder values documented inline.
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
namespace talker
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
	// Milan Vendor Unique (MVU) commands — GET_MILAN_INFO declares Milan compliance (GH #15 / M4).
	virtual bool onUnhandledAecpVuCommand(protocol::ProtocolInterface* const pi, protocol::VuAecpdu::ProtocolIdentifier const& protocolIdentifier, protocol::Aecpdu const& aecpdu) noexcept override;
	/* **** ACMP notifications **** */
	virtual void onAcmpCommand(protocol::ProtocolInterface* const pi, protocol::Acmpdu const& acmpdu) noexcept override;

	/* ************************************************************************** */
	/* ACMP talker state machine helpers                                          */
	/* ************************************************************************** */
	std::uint16_t streamOutputCount() const noexcept;
	// LOCK_ENTITY (mandatory for Milan) — exclusive-control lock held by one controller at a time.
	void handleLockEntity(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& aem) noexcept;
	// Build + send the GET_MILAN_INFO response (Milan compliance declaration).
	void sendMilanInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	// Milan 1.3 mandatory dynamic info (GH #15 / #167). Each returns true if it answered, or
	// false to let the local entity reflect NotImplemented (e.g. an unknown descriptor index).
	bool sendMediaClockReferenceInfoResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	bool sendStreamInputInfoExResponse(protocol::ProtocolInterface* const pi, protocol::MvuAecpdu const& command) const noexcept;
	static networkInterface::MacAddress talkerMacFromEntity(Entity const& entity) noexcept;
	// On-wire stream identifiers, matching avtpd for the default split32 profile:
	//   stream_id = talker MAC (6 bytes) << 16 | stream index   (standard AVTP stream_id)
	//   dest_mac  = avtpd static-MAAP base 91:e0:f0:00:fe:00 + stream index
	//   vlan      = 2 (SR class A)
	// dest_mac base + vlan are still the avtpd compile defaults; an operator override of
	// avtpd.tx.streamDestinationMacBase/vlanVid would need wiring from the profile (M5b).
	// Map a STREAM_OUTPUT descriptor index (the ACMP talker_unique_id) to its on-wire AVTP stream
	// uid. Identity unless setTalkerStreamOutputWireUids() registered a divergent mapping (e.g. CRF).
	std::uint16_t wireUidFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept;
	std::uint64_t streamIdFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept;
	networkInterface::MacAddress streamDestMacFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept;
	void sendTalkerResponse(protocol::ProtocolInterface* const pi, protocol::Acmpdu const& command, protocol::AcmpMessageType const responseType, protocol::AcmpStatus const status, UniqueIdentifier const listenerEntityID, protocol::AcmpUniqueID const listenerUniqueID, std::uint16_t const connectionCount) const noexcept;

	/* ************************************************************************** */
	/* Internal variables                                                         */
	/* ************************************************************************** */
	protocol::ProtocolInterface* const _protocolInterface{ nullptr };
	UniqueIdentifier const _entityID{ UniqueIdentifier::getNullUniqueIdentifier() };
	networkInterface::MacAddress const _talkerMac{};
	model::EntityTree const* const _entityModelTree{ nullptr };
	// Per-STREAM_OUTPUT-descriptor on-wire AVTP stream uid (see setTalkerStreamOutputWireUids).
	// Declared before _aemHandler so it is constructed first and can be handed to it.
	std::vector<std::uint16_t> const _streamOutputWireUids;
	model::AemHandler const _aemHandler;
	// Invoked on CONNECT_TX/DISCONNECT_TX with (talker stream index, connected-listener count) to
	// drive the avtpd transmit gate. Runs on the protocol-interface thread. See aggregateEntity.hpp.
	std::function<void(std::uint16_t, std::uint16_t)> const _connectionObserver;

	// Per-talker-stream connection state: talker stream unique id -> connected listeners.
	struct ListenerPair
	{
		UniqueIdentifier entityID{};
		protocol::AcmpUniqueID uniqueID{ 0u };
	};
	mutable std::mutex _connectionsMutex;
	std::unordered_map<protocol::AcmpUniqueID, std::vector<ListenerPair>> _connections;

	// LOCK_ENTITY state: the controller currently holding the exclusive lock (null = unlocked).
	mutable std::mutex _lockMutex;
	UniqueIdentifier _lockHolder{};
};

} // namespace talker
} // namespace entity
} // namespace avdecc
} // namespace la
