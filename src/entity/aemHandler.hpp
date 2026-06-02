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
* @file aemHandler.hpp
* @author Christophe Calmejane
*/

#pragma once

#include "la/avdecc/internals/entity.hpp"
#include "la/avdecc/internals/entityModelTree.hpp"
#include "la/avdecc/internals/protocolInterface.hpp"
#include "la/avdecc/internals/protocolAemAecpdu.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace la
{
namespace avdecc
{
namespace entity
{
namespace model
{
class AemHandler final
{
public:
	// streamOutputWireUids (3SB additive, GH #15): per STREAM_OUTPUT descriptor index, the on-wire
	// AVTP stream uid reported in GET_STREAM_INFO. Empty (controller use) => identity (uid = index).
	// countersProvider (3SB additive, GH #15 / M5): answers STREAM_OUTPUT GET_COUNTERS; empty =>
	// GET_COUNTERS responds NotImplemented.
	using CountersProvider = std::function<bool(std::uint16_t talkerUniqueID, entity::model::DescriptorCounterValidFlag& validCounters, entity::model::DescriptorCounters& counters)>;
	AemHandler(entity::Entity const& entity, entity::model::EntityTree const* const entityModelTree, std::vector<std::uint16_t> streamOutputWireUids = {}, CountersProvider countersProvider = {});

	static void validateEntityModel(entity::model::EntityTree const* const entityModelTree);

	bool onUnhandledAecpAemCommand(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& aem) const noexcept;

	// Deleted compiler auto-generated methods
	AemHandler(AemHandler const&) = delete;
	AemHandler(AemHandler&&) = delete;
	AemHandler& operator=(AemHandler const&) = delete;
	AemHandler& operator=(AemHandler&&) = delete;

private:
	EntityDescriptor buildEntityDescriptor() const noexcept;
	ConfigurationDescriptor buildConfigurationDescriptor(entity::model::ConfigurationIndex const configIndex) const;
	// 3SB additive (GH #15): descriptor builders for the talker entity responder so a
	// controller (e.g. Hive) can enumerate the full configuration tree. Each throws
	// NoSuchDescriptorException when the requested index is absent from the tree.
	AudioUnitDescriptor buildAudioUnitDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::AudioUnitIndex const audioUnitIndex) const;
	StreamDescriptor buildStreamOutputDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::StreamIndex const streamIndex) const;
	StreamDescriptor buildStreamInputDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::StreamIndex const streamIndex) const;
	AvbInterfaceDescriptor buildAvbInterfaceDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::AvbInterfaceIndex const avbInterfaceIndex) const;
	ClockSourceDescriptor buildClockSourceDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::ClockSourceIndex const clockSourceIndex) const;
	ClockDomainDescriptor buildClockDomainDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::ClockDomainIndex const clockDomainIndex) const;
	LocaleDescriptor buildLocaleDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::LocaleIndex const localeIndex) const;
	StringsDescriptor buildStringsDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::StringsIndex const stringsIndex) const;
	StreamPortDescriptor buildStreamPortOutputDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::StreamPortIndex const streamPortIndex) const;
	AudioClusterDescriptor buildAudioClusterDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::ClusterIndex const clusterIndex) const;
	AudioMapDescriptor buildAudioMapDescriptor(entity::model::ConfigurationIndex const configIndex, entity::model::MapIndex const mapIndex) const;

	entity::Entity const& _entity;
	entity::model::EntityTree const* _entityModelTree{ nullptr };
	std::vector<std::uint16_t> _streamOutputWireUids;
	CountersProvider _countersProvider;
};

} // namespace model
} // namespace entity
} // namespace avdecc
} // namespace la
