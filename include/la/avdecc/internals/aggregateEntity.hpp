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
* @file aggregateEntity.hpp
* @author Christophe Calmejane
* @brief Avdecc aggregate entity (supporting multiple types for the same EntityID).
*/

#pragma once

#include "la/avdecc/memoryBuffer.hpp"

#include "entity.hpp"
#include "entityModel.hpp"
#include "entityModelTree.hpp"
#include "entityAddressAccessTypes.hpp"
#include "controllerEntity.hpp"
#include "exports.hpp"

#include <thread>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

namespace la
{
namespace avdecc
{
namespace entity
{
/** 3SB additive (GH #15): register, per STREAM_OUTPUT descriptor index, the on-wire AVTP stream
  * uid the talker responder must advertise in ACMP CONNECT_TX / GET_STREAM_INFO. The on-wire
  * stream_id is talkerMAC(48) << 16 | uid and the SR multicast dest_mac is the MAAP base + uid.
  * For most streams uid == descriptor index, but the data plane (avtpd) may assign a stream a uid
  * that differs from its descriptor index (e.g. the CRF media-clock stream sits at descriptor index
  * N yet transmits on wire uid 8) — a listener that connects then registers for the wrong stream_id
  * and never locks. Call this BEFORE AggregateEntity::create() for the same entityID; the mapping is
  * consumed at entity construction. A missing entry, or an index beyond the vector, means identity. */
LA_AVDECC_API void LA_AVDECC_CALL_CONVENTION setTalkerStreamOutputWireUids(UniqueIdentifier const entityID, std::vector<std::uint16_t> const& wireUids) noexcept;

/** 3SB additive: per-STREAM_OUTPUT presentation time offset (ns), indexed by descriptor index, used
  * to populate GET_STREAM_INFO msrp_accumulated_latency (+ MsrpAccLatValid) and GET_MAX_TRANSIT_TIME
  * so a controller can read the talker's presentation time. Call BEFORE AggregateEntity::create();
  * consumed at construction (the descriptor dynamic model is cleared by la_avdecc, so the value must
  * live on the handler). A missing entry, or an index beyond the vector, or 0, means "not reported". */
LA_AVDECC_API void LA_AVDECC_CALL_CONVENTION setTalkerStreamOutputPresentationOffsetsNs(UniqueIdentifier const entityID, std::vector<std::uint32_t> const& offsetsNs) noexcept;

/** 3SB additive (GH #15 / M5): observer invoked when a listener connects to or disconnects from a
  * talker STREAM_OUTPUT (ACMP CONNECT_TX / DISCONNECT_TX). Arguments: the talker stream descriptor
  * index and the resulting connected-listener count (0 == no listeners). Used to drive the avtpd
  * transmit gate under transmitPolicy=avdecc. Register BEFORE AggregateEntity::create(); consumed at
  * construction. The callback runs on the protocol-interface thread — keep it short and non-blocking. */
using TalkerConnectionObserver = std::function<void(std::uint16_t talkerUniqueID, std::uint16_t connectionCount)>;
LA_AVDECC_API void LA_AVDECC_CALL_CONVENTION setTalkerConnectionObserver(UniqueIdentifier const entityID, TalkerConnectionObserver observer) noexcept;

/** 3SB additive (GH #15 / M5): provider the talker responder calls to answer GET_COUNTERS for a
  * STREAM_OUTPUT. Given the talker stream descriptor index, fill the valid-flags mask + the 32-entry
  * counter array and return true (false => respond NotImplemented). Lets the daemon surface avtpd's
  * per-stream counters. Register BEFORE AggregateEntity::create(); consumed at construction. Runs on
  * the protocol-interface thread — keep it short and non-blocking. */
using TalkerCountersProvider = std::function<bool(std::uint16_t talkerUniqueID, model::DescriptorCounterValidFlag& validCounters, model::DescriptorCounters& counters)>;
LA_AVDECC_API void LA_AVDECC_CALL_CONVENTION setTalkerCountersProvider(UniqueIdentifier const entityID, TalkerCountersProvider provider) noexcept;

/** 3SB additive (software-mode P2): observer invoked when the listener binds or unbinds a
  * STREAM_INPUT via the ACMP listener state machine (CONNECT_RX / DISCONNECT_RX), after the talker
  * handshake resolves. Arguments: the listener stream descriptor index, whether it is now bound, the
  * on-wire AVTP stream_id, and the ACMP flags. Lets the daemon point the AAF receiver at the bound
  * talker stream. Register BEFORE AggregateEntity::create(); consumed at construction. Runs on the
  * protocol-interface thread — keep it short and non-blocking. */
using ListenerBindObserver = std::function<void(std::uint16_t listenerUniqueID, bool bound, std::uint64_t streamID)>;
LA_AVDECC_API void LA_AVDECC_CALL_CONVENTION setListenerBindObserver(UniqueIdentifier const entityID, ListenerBindObserver observer) noexcept;

class AggregateEntity : public LocalEntity, public controller::Interface
{
public:
	using UniquePointer = std::unique_ptr<AggregateEntity, void (*)(AggregateEntity*)>;

	/**
	* @brief Factory method to create a new AggregateEntity.
	* @details Creates a new AggregateEntity as a unique pointer.
	* @param[in] protocolInterface The protocol interface to bind the entity to.
	* @param[in] commonInformation Common information for this aggregate entity.
	* @param[in] interfacesInformation All interfaces information for this aggregate entity.
	* @param[in] entityModelTree The entity model tree to use for this controller entity, or null to not expose a model.
	* @param[in] controllerDelegate The Delegate to be called whenever a controller related notification occurs.
	* @return A new AggregateEntity as a Entity::UniquePointer.
	* @note Might throw an Exception.
	*/
	static UniquePointer create(protocol::ProtocolInterface* const protocolInterface, CommonInformation const& commonInformation, InterfacesInformation const& interfacesInformation, model::EntityTree const* const entityModelTree, controller::Delegate* const controllerDelegate)
	{
		auto deleter = [](AggregateEntity* self)
		{
			self->destroy();
		};
		return UniquePointer(createRawAggregateEntity(protocolInterface, commonInformation, interfacesInformation, entityModelTree, controllerDelegate), deleter);
	}

	/* Discovery Protocol (ADP) */
	/** Enables entity advertising with available duration included between 2-62 seconds on the specified interfaceIndex if set, otherwise on all interfaces. Returns false if EntityID is already in use on the local computer, true otherwise. */
	using LocalEntity::enableEntityAdvertising;
	/** Disables entity advertising on the specified interfaceIndex if set, otherwise on all interfaces. */
	using LocalEntity::disableEntityAdvertising;
	/** Requests a remote entities discovery. */
	using LocalEntity::discoverRemoteEntities;
	/** Requests a targetted remote entity discovery. */
	using LocalEntity::discoverRemoteEntity;
	/** Forgets the specified remote entity. */
	using LocalEntity::forgetRemoteEntity;
	/** Sets automatic discovery delay. 0 (default) for no automatic discovery. */
	using LocalEntity::setAutomaticDiscoveryDelay;

	virtual void setControllerDelegate(controller::Delegate* const delegate) noexcept = 0;
	//virtual void setListenerDelegate(listener::Delegate* const delegate) noexcept = 0;
	//virtual void setTalkerDelegate(talker::Delegate* const delegate) noexcept = 0;

	// Deleted compiler auto-generated methods
	AggregateEntity(AggregateEntity&&) = delete;
	AggregateEntity(AggregateEntity const&) = delete;
	AggregateEntity& operator=(AggregateEntity const&) = delete;
	AggregateEntity& operator=(AggregateEntity&&) = delete;

protected:
	/** Constructor */
	AggregateEntity(CommonInformation const& commonInformation, InterfacesInformation const& interfacesInformation);

	/** Destructor */
	virtual ~AggregateEntity() noexcept = default;

private:
	/** Entry point */
	static LA_AVDECC_API AggregateEntity* LA_AVDECC_CALL_CONVENTION createRawAggregateEntity(protocol::ProtocolInterface* const protocolInterface, CommonInformation const& commonInformation, InterfacesInformation const& interfacesInformation, model::EntityTree const* const entityModelTree, controller::Delegate* const controllerDelegate);

	/** Destroy method for COM-like interface */
	virtual void destroy() noexcept = 0;
};

} // namespace entity
} // namespace avdecc
} // namespace la
