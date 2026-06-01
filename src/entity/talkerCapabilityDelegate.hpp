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
* additive implementation provides the entity-responder side so a talker-capable
* AggregateEntity can be constructed and advertised, and so incoming AECP AEM
* commands (notably READ_DESCRIPTOR) are answered via the shared AemHandler --
* mirroring controller::CapabilityDelegate exactly. ACMP (the talker connection
* state machine) and the broader AEM command set are intentionally not yet
* implemented here (3SB migration milestones M2/M3); for now ACMP is a no-op and
* unhandled AEM commands fall through to la_avdecc's default NotImplemented path.
*/

#pragma once

#include "la/avdecc/internals/entityModelTree.hpp"

#include "entityImpl.hpp"
#include "aemHandler.hpp"

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
	/* **** ACMP notifications **** */
	// M1: ACMP talker state machine not yet implemented (milestone M3). Connect/
	// Disconnect/GetState are left as the base-class no-ops.

	/* ************************************************************************** */
	/* Internal variables                                                         */
	/* ************************************************************************** */
	protocol::ProtocolInterface* const _protocolInterface{ nullptr };
	UniqueIdentifier const _entityID{ UniqueIdentifier::getNullUniqueIdentifier() };
	model::AemHandler const _aemHandler;
};

} // namespace talker
} // namespace entity
} // namespace avdecc
} // namespace la
