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

#include <algorithm>
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
	, _talkerMac{ talkerMacFromEntity(entity) }
	, _entityModelTree{ entityModelTree }
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

std::uint64_t CapabilityDelegate::streamIdFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept
{
	// Standard AVTP stream_id = talker MAC (48 bits) << 16 | stream index. This is what
	// avtpd transmits with, so a listener that connects via this response will receive.
	std::uint64_t macU48 = 0u;
	for (auto const octet : _talkerMac)
	{
		macU48 = (macU48 << 8) | static_cast<std::uint64_t>(octet);
	}
	return (macU48 << 16) | static_cast<std::uint64_t>(talkerUniqueID);
}

networkInterface::MacAddress CapabilityDelegate::streamDestMacFor(protocol::AcmpUniqueID const talkerUniqueID) const noexcept
{
	// Placeholder: avtpd static-MAAP base 91:e0:f0:00:fe:00 + stream index (M5 wires the real value).
	return networkInterface::MacAddress{ { 0x91, 0xe0, 0xf0, 0x00, 0xfe, static_cast<std::uint8_t>(talkerUniqueID & 0xFFu) } };
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
	}
	else if (messageType == protocol::AcmpMessageType::DisconnectTxCommand)
	{
		listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [&](ListenerPair const& p) { return p.entityID == listenerEntityID && p.uniqueID == listenerUniqueID; }), listeners.end());
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, listenerEntityID, listenerUniqueID, static_cast<std::uint16_t>(listeners.size()));
	}
	else if (messageType == protocol::AcmpMessageType::GetTxStateCommand)
	{
		sendTalkerResponse(pi, acmpdu, responseType, protocol::AcmpStatus::Success, listenerEntityID, listenerUniqueID, static_cast<std::uint16_t>(listeners.size()));
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
