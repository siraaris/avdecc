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

#include "talkerCapabilityDelegate.hpp"

#include <exception>

namespace la
{
namespace avdecc
{
namespace entity
{
namespace talker
{
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
	, _aemHandler{ entity, entityModelTree }
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

		// Delegate descriptor reads (and any other AemHandler-supported commands)
		// to the shared AemHandler, exactly as controller::CapabilityDelegate does.
		return _aemHandler.onUnhandledAecpAemCommand(pi, aem);
	}
	return false;
}

} // namespace talker
} // namespace entity
} // namespace avdecc
} // namespace la
