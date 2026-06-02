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
* @file aemHandler.cpp
* @author Christophe Calmejane
*/

#include "aemHandler.hpp"
#include "entityImpl.hpp"
#include "protocol/protocolAemPayloads.hpp"


namespace la
{
namespace avdecc
{
namespace entity
{
namespace model
{
class NoSuchDescriptorException final : public Exception
{
public:
	NoSuchDescriptorException()
		: Exception("No such descriptor")
	{
	}
};

AemHandler::AemHandler(entity::Entity const& entity, entity::model::EntityTree const* const entityModelTree, std::vector<std::uint16_t> streamOutputWireUids)
	: _entity{ entity }
	, _entityModelTree{ entityModelTree }
	, _streamOutputWireUids{ std::move(streamOutputWireUids) }
{
	// Valide the entity model
	validateEntityModel(_entityModelTree);
}

void AemHandler::validateEntityModel(entity::model::EntityTree const* const entityModelTree)
{
	// Briefly validate entity model
	if (entityModelTree != nullptr)
	{
		// Check there is at least one configuration descriptor
		auto const countConfigs = static_cast<DescriptorIndex>(entityModelTree->configurationTrees.size());
		if (countConfigs == 0)
		{
			throw Exception("Invalid Entity Model: At least one ConfigurationDescriptor is required");
		}

		// Check the current configuration index is in the correct range
		if (entityModelTree->dynamicModel.currentConfiguration >= countConfigs)
		{
			throw Exception("Invalid Entity Model: Current Configuration Index is out of range");
		}
	}
}

bool AemHandler::onUnhandledAecpAemCommand(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& aem) const noexcept
{
	static std::unordered_map<protocol::AemCommandType::value_type, std::function<bool(protocol::ProtocolInterface* const pi, AemHandler const& aemHandler, protocol::AemAecpdu const& aem)>> s_Dispatch{
		// Read Descriptor
		{ protocol::AemCommandType::ReadDescriptor.getValue(),
			[](protocol::ProtocolInterface* const pi, AemHandler const& aemHandler, protocol::AemAecpdu const& aem)
			{
				if (aemHandler._entityModelTree != nullptr)
				{
					auto const [configIndex, descriptorType, descriptorIndex] = protocol::aemPayload::deserializeReadDescriptorCommand(aem.getPayload());
					switch (descriptorType)
					{
						case DescriptorType::Entity:
						{
							if (configIndex != DescriptorIndex{ 0u } || descriptorIndex != DescriptorIndex{ 0u })
							{
								LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::BadArguments);
								return true;
							}
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadEntityDescriptorResponse(ser, aemHandler.buildEntityDescriptor());
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::Configuration:
						{
							if (configIndex != DescriptorIndex{ 0u })
							{
								LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::BadArguments);
								return true;
							}
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadConfigurationDescriptorResponse(ser, aemHandler.buildConfigurationDescriptor(descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						// 3SB additive (GH #15): full descriptor-tree coverage for the talker
						// entity responder. Each builder throws NoSuchDescriptorException when
						// the index is absent (translated to NoSuchDescriptor by the catch below).
						case DescriptorType::AudioUnit:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadAudioUnitDescriptorResponse(ser, aemHandler.buildAudioUnitDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::StreamInput:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadStreamDescriptorResponse(ser, aemHandler.buildStreamInputDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::StreamOutput:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadStreamDescriptorResponse(ser, aemHandler.buildStreamOutputDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::AvbInterface:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadAvbInterfaceDescriptorResponse(ser, aemHandler.buildAvbInterfaceDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::ClockSource:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadClockSourceDescriptorResponse(ser, aemHandler.buildClockSourceDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::ClockDomain:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadClockDomainDescriptorResponse(ser, aemHandler.buildClockDomainDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::StreamPortOutput:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadStreamPortDescriptorResponse(ser, aemHandler.buildStreamPortOutputDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::AudioCluster:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadAudioClusterDescriptorResponse(ser, aemHandler.buildAudioClusterDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::AudioMap:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadAudioMapDescriptorResponse(ser, aemHandler.buildAudioMapDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::Locale:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadLocaleDescriptorResponse(ser, aemHandler.buildLocaleDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						case DescriptorType::Strings:
						{
							auto ser = protocol::aemPayload::serializeReadDescriptorCommonResponse(configIndex, descriptorType, descriptorIndex);
							protocol::aemPayload::serializeReadStringsDescriptorResponse(ser, aemHandler.buildStringsDescriptor(configIndex, descriptorIndex));
							LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
							return true;
						}
						default:
							break;
					}
				}
				return false;
			} },
		// 3SB additive (GH #15 / M2): dynamic AEM commands the controller (Hive) queries during
		// enumeration. Sourced from static/derived values, since la_avdecc clears the EntityTree's
		// nested dynamic models at entity construction (top-level dynamic is preserved).
		// GET_CONFIGURATION
		{ protocol::AemCommandType::GetConfiguration.getValue(),
			[](protocol::ProtocolInterface* const pi, AemHandler const& aemHandler, protocol::AemAecpdu const& aem)
			{
				if (aemHandler._entityModelTree == nullptr)
				{
					return false;
				}
				auto ser = protocol::aemPayload::serializeGetConfigurationResponse(aemHandler._entityModelTree->dynamicModel.currentConfiguration);
				LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
				return true;
			} },
		// GET_STREAM_FORMAT (StreamInput / StreamOutput)
		{ protocol::AemCommandType::GetStreamFormat.getValue(),
			[](protocol::ProtocolInterface* const pi, AemHandler const& aemHandler, protocol::AemAecpdu const& aem)
			{
				if (aemHandler._entityModelTree == nullptr)
				{
					return false;
				}
				auto const [descriptorType, streamIndex] = protocol::aemPayload::deserializeGetStreamFormatCommand(aem.getPayload());
				auto const configIndex = aemHandler._entityModelTree->dynamicModel.currentConfiguration;
				auto const streamDescriptor = (descriptorType == DescriptorType::StreamOutput) ? aemHandler.buildStreamOutputDescriptor(configIndex, streamIndex) : aemHandler.buildStreamInputDescriptor(configIndex, streamIndex);
				auto ser = protocol::aemPayload::serializeGetStreamFormatResponse(descriptorType, streamIndex, streamDescriptor.currentFormat);
				LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
				return true;
			} },
		// GET_STREAM_INFO (StreamInput / StreamOutput) - IEEE1722.1-2013 base form.
		// Carries the on-wire stream identification (stream_id / dest_mac / format / vlan) a
		// controller (Hive) uses to resolve a talker stream to a live, network-present node.
		// Without it Hive cannot correlate the stream to SRP/network state and renders any
		// connection against its "OfflineOutputStream" virtual node ("Talker not detected on the
		// Network"). Identifiers match avtpd for the default split32 profile:
		//   stream_id = talker MAC (6 bytes) << 16 | stream index
		//   dest_mac  = avtpd static-MAAP base 91:e0:f0:00:fe:00 + stream index
		//   vlan      = 2 (SR class A). dest_mac base + vlan are the avtpd compile defaults; an
		// operator override would need wiring from the profile (tracked with M5b).
		{ protocol::AemCommandType::GetStreamInfo.getValue(),
			[](protocol::ProtocolInterface* const pi, AemHandler const& aemHandler, protocol::AemAecpdu const& aem)
			{
				if (aemHandler._entityModelTree == nullptr)
				{
					return false;
				}
				auto const [descriptorType, streamIndex] = protocol::aemPayload::deserializeGetStreamInfoCommand(aem.getPayload());
				auto const configIndex = aemHandler._entityModelTree->dynamicModel.currentConfiguration;
				auto const streamDescriptor = (descriptorType == DescriptorType::StreamOutput) ? aemHandler.buildStreamOutputDescriptor(configIndex, streamIndex) : aemHandler.buildStreamInputDescriptor(configIndex, streamIndex);

				auto streamInfo = entity::model::StreamInfo{};
				std::uint64_t macU48 = 0u;
				auto const& interfaces = aemHandler._entity.getInterfacesInformation();
				if (!interfaces.empty())
				{
					for (auto const octet : interfaces.begin()->second.macAddress)
					{
						macU48 = (macU48 << 8) | static_cast<std::uint64_t>(octet);
					}
				}
				// Map descriptor index -> on-wire AVTP stream uid (identity unless the data plane
				// numbers the stream separately, e.g. CRF). Must match the ACMP CONNECT_TX response.
				auto const wireUid = (streamIndex < aemHandler._streamOutputWireUids.size()) ? aemHandler._streamOutputWireUids[streamIndex] : static_cast<std::uint16_t>(streamIndex);
				streamInfo.streamFormat = streamDescriptor.currentFormat;
				streamInfo.streamID = UniqueIdentifier{ (macU48 << 16) | static_cast<std::uint64_t>(wireUid) };
				streamInfo.streamDestMac = networkInterface::MacAddress{ { 0x91, 0xe0, 0xf0, 0x00, 0xfe, static_cast<std::uint8_t>(wireUid & 0xFFu) } };
				streamInfo.streamVlanID = std::uint16_t{ 2u };
				// Flag the identification fields valid so the controller treats the stream as a real
				// network talker. (Connected/MSRP latency need per-stream connection state we do not
				// track in the AemHandler yet — wired with the ACMP/counters work.)
				streamInfo.streamInfoFlags = entity::StreamInfoFlags{ entity::StreamInfoFlag::StreamFormatValid, entity::StreamInfoFlag::StreamIDValid, entity::StreamInfoFlag::StreamDestMacValid, entity::StreamInfoFlag::StreamVlanIDValid };

				auto ser = protocol::aemPayload::serializeGetStreamInfoResponse(descriptorType, streamIndex, streamInfo);
				LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
				return true;
			} },
		// GET_MAX_TRANSIT_TIME (StreamOutput) - IEEE1722.1-2021. We don't track a per-stream
		// transit time yet; report 0 so the controller stops flagging an invalid response.
		{ protocol::AemCommandType::GetMaxTransitTime.getValue(),
			[](protocol::ProtocolInterface* const pi, AemHandler const& /*aemHandler*/, protocol::AemAecpdu const& aem)
			{
				auto const [descriptorType, streamIndex] = protocol::aemPayload::deserializeGetMaxTransitTimeCommand(aem.getPayload());
				auto ser = protocol::aemPayload::serializeGetMaxTransitTimeResponse(descriptorType, streamIndex, std::uint64_t{ 0u });
				LocalEntityImpl<>::sendAemAecpResponse(pi, aem, protocol::AemAecpStatus::Success, ser.data(), ser.size());
				return true;
			} },
	};

	auto const& it = s_Dispatch.find(aem.getCommandType().getValue());
	if (it != s_Dispatch.end())
	{
		try
		{
			return it->second(pi, *this, aem);
		}
		catch (NoSuchDescriptorException const&)
		{
			LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::NoSuchDescriptor);
			return true;
		}
		catch (la::avdecc::Exception const&)
		{
			LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::EntityMisbehaving);
			return true;
		}
		catch (...)
		{
			AVDECC_ASSERT(false, "Unexpected exception");
			LocalEntityImpl<>::reflectAecpCommand(pi, aem, protocol::AemAecpStatus::EntityMisbehaving);
			return true;
		}
	}

	return false;
}

EntityDescriptor AemHandler::buildEntityDescriptor() const noexcept
{
	auto entityDescriptor = EntityDescriptor{};


	entityDescriptor.entityID = _entity.getEntityID();
	entityDescriptor.entityModelID = _entity.getEntityModelID();
	entityDescriptor.entityCapabilities = _entity.getEntityCapabilities();
	entityDescriptor.talkerStreamSources = _entity.getTalkerStreamSources();
	entityDescriptor.talkerCapabilities = _entity.getTalkerCapabilities();
	entityDescriptor.listenerStreamSinks = _entity.getListenerStreamSinks();
	entityDescriptor.listenerCapabilities = _entity.getListenerCapabilities();
	entityDescriptor.controllerCapabilities = _entity.getControllerCapabilities();
	entityDescriptor.availableIndex = 0u;
	if (auto const id = _entity.getAssociationID(); id.has_value())
	{
		entityDescriptor.associationID = *id;
	}
	entityDescriptor.entityName = _entityModelTree->dynamicModel.entityName;
	entityDescriptor.vendorNameString = _entityModelTree->staticModel.vendorNameString;
	entityDescriptor.modelNameString = _entityModelTree->staticModel.modelNameString;
	entityDescriptor.firmwareVersion = _entityModelTree->dynamicModel.firmwareVersion;
	entityDescriptor.groupName = _entityModelTree->dynamicModel.groupName;
	entityDescriptor.serialNumber = _entityModelTree->dynamicModel.serialNumber;
	entityDescriptor.configurationsCount = static_cast<decltype(entityDescriptor.configurationsCount)>(_entityModelTree->configurationTrees.size());
	entityDescriptor.currentConfiguration = _entityModelTree->dynamicModel.currentConfiguration;

	return entityDescriptor;
}

template<DescriptorType DescriptorT, class Tree>
void setDescriptorsCount(ConfigurationDescriptor& configDescriptor, Tree const& tree)
{
	if (!tree.empty())
	{
		configDescriptor.descriptorCounts[DescriptorT] = static_cast<typename decltype(configDescriptor.descriptorCounts)::mapped_type>(tree.size());
	}
}

ConfigurationDescriptor AemHandler::buildConfigurationDescriptor(entity::model::ConfigurationIndex const configIndex) const
{
	auto configDescriptor = ConfigurationDescriptor{};

	auto const configIt = _entityModelTree->configurationTrees.find(configIndex);
	if (configIt == _entityModelTree->configurationTrees.end())
	{
		throw NoSuchDescriptorException{};
	}

	auto const& configTree = configIt->second;

	configDescriptor.objectName = configTree.dynamicModel.objectName;
	configDescriptor.localizedDescription = configTree.staticModel.localizedDescription;

	setDescriptorsCount<DescriptorType::AudioUnit>(configDescriptor, configTree.audioUnitTrees);
	setDescriptorsCount<DescriptorType::StreamInput>(configDescriptor, configTree.streamInputModels);
	setDescriptorsCount<DescriptorType::StreamOutput>(configDescriptor, configTree.streamOutputModels);
	setDescriptorsCount<DescriptorType::JackInput>(configDescriptor, configTree.jackInputTrees);
	setDescriptorsCount<DescriptorType::JackOutput>(configDescriptor, configTree.jackOutputTrees);
	setDescriptorsCount<DescriptorType::AvbInterface>(configDescriptor, configTree.avbInterfaceModels);
	setDescriptorsCount<DescriptorType::ClockSource>(configDescriptor, configTree.clockSourceModels);
	setDescriptorsCount<DescriptorType::MemoryObject>(configDescriptor, configTree.memoryObjectModels);
	setDescriptorsCount<DescriptorType::Locale>(configDescriptor, configTree.localeTrees);
	setDescriptorsCount<DescriptorType::Control>(configDescriptor, configTree.controlModels);
	setDescriptorsCount<DescriptorType::ClockDomain>(configDescriptor, configTree.clockDomainModels);
	setDescriptorsCount<DescriptorType::Timing>(configDescriptor, configTree.timingModels);
	setDescriptorsCount<DescriptorType::PtpInstance>(configDescriptor, configTree.ptpInstanceTrees);

	std::unordered_map<DescriptorType, std::uint16_t, la::avdecc::utils::EnumClassHash> descriptorCounts{};

	return configDescriptor;
}

/* ************************************************************************** */
/* 3SB additive (GH #15): descriptor builders for the talker entity responder */
/* ************************************************************************** */
namespace
{
ConfigurationTree const& getConfigurationTree(EntityTree const* const tree, ConfigurationIndex const configIndex)
{
	if (tree == nullptr)
	{
		throw NoSuchDescriptorException{};
	}
	auto const it = tree->configurationTrees.find(configIndex);
	if (it == tree->configurationTrees.end())
	{
		throw NoSuchDescriptorException{};
	}
	return it->second;
}

StreamDescriptor makeStreamDescriptor(StreamNodeStaticModel const& s, StreamNodeDynamicModel const& d)
{
	auto desc = StreamDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.clockDomainIndex = s.clockDomainIndex;
	desc.streamFlags = s.streamFlags;
	// la_avdecc does not carry the nested-descriptor *dynamic* models from the provided
	// EntityTree into the live entity (only static models + top-level entity dynamic), so
	// the dynamic streamFormat is null here. For a fixed-format talker the current format
	// is the (sole) supported format, so fall back to the first static format. Without a
	// valid currentFormat, controllers (Hive) divide-by-zero on the stream's nominal rate.
	desc.currentFormat = d.streamFormat;
	if (!desc.currentFormat && !s.formats.empty())
	{
		desc.currentFormat = *s.formats.begin();
	}
	desc.backupTalkerEntityID_0 = s.backupTalkerEntityID_0;
	desc.backupTalkerUniqueID_0 = s.backupTalkerUniqueID_0;
	desc.backupTalkerEntityID_1 = s.backupTalkerEntityID_1;
	desc.backupTalkerUniqueID_1 = s.backupTalkerUniqueID_1;
	desc.backupTalkerEntityID_2 = s.backupTalkerEntityID_2;
	desc.backupTalkerUniqueID_2 = s.backupTalkerUniqueID_2;
	desc.backedupTalkerEntityID = s.backedupTalkerEntityID;
	desc.backedupTalkerUnique = s.backedupTalkerUnique;
	desc.avbInterfaceIndex = s.avbInterfaceIndex;
	desc.bufferLength = s.bufferLength;
	desc.formats = s.formats;
	return desc;
}

StreamPortTree const* findStreamPortOutput(ConfigurationTree const& cfg, StreamPortIndex const streamPortIndex)
{
	for (auto const& [audioUnitIndex, audioUnit] : cfg.audioUnitTrees)
	{
		auto const it = audioUnit.streamPortOutputTrees.find(streamPortIndex);
		if (it != audioUnit.streamPortOutputTrees.end())
		{
			return &it->second;
		}
	}
	return nullptr;
}

AudioClusterNodeModels const* findAudioCluster(ConfigurationTree const& cfg, ClusterIndex const clusterIndex)
{
	for (auto const& [audioUnitIndex, audioUnit] : cfg.audioUnitTrees)
	{
		for (auto const* const trees : { &audioUnit.streamPortOutputTrees, &audioUnit.streamPortInputTrees })
		{
			for (auto const& [streamPortIndex, streamPort] : *trees)
			{
				auto const it = streamPort.audioClusterModels.find(clusterIndex);
				if (it != streamPort.audioClusterModels.end())
				{
					return &it->second;
				}
			}
		}
	}
	return nullptr;
}

AudioMapNodeModels const* findAudioMap(ConfigurationTree const& cfg, MapIndex const mapIndex)
{
	for (auto const& [audioUnitIndex, audioUnit] : cfg.audioUnitTrees)
	{
		for (auto const* const trees : { &audioUnit.streamPortOutputTrees, &audioUnit.streamPortInputTrees })
		{
			for (auto const& [streamPortIndex, streamPort] : *trees)
			{
				auto const it = streamPort.audioMapModels.find(mapIndex);
				if (it != streamPort.audioMapModels.end())
				{
					return &it->second;
				}
			}
		}
	}
	return nullptr;
}
} // namespace

AudioUnitDescriptor AemHandler::buildAudioUnitDescriptor(ConfigurationIndex const configIndex, AudioUnitIndex const audioUnitIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.audioUnitTrees.find(audioUnitIndex);
	if (it == cfg.audioUnitTrees.end())
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = it->second.staticModel;
	auto const& d = it->second.dynamicModel;
	auto desc = AudioUnitDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.clockDomainIndex = s.clockDomainIndex;
	desc.numberOfStreamInputPorts = s.numberOfStreamInputPorts;
	desc.baseStreamInputPort = s.baseStreamInputPort;
	desc.numberOfStreamOutputPorts = s.numberOfStreamOutputPorts;
	desc.baseStreamOutputPort = s.baseStreamOutputPort;
	desc.numberOfExternalInputPorts = s.numberOfExternalInputPorts;
	desc.baseExternalInputPort = s.baseExternalInputPort;
	desc.numberOfExternalOutputPorts = s.numberOfExternalOutputPorts;
	desc.baseExternalOutputPort = s.baseExternalOutputPort;
	desc.numberOfInternalInputPorts = s.numberOfInternalInputPorts;
	desc.baseInternalInputPort = s.baseInternalInputPort;
	desc.numberOfInternalOutputPorts = s.numberOfInternalOutputPorts;
	desc.baseInternalOutputPort = s.baseInternalOutputPort;
	desc.numberOfControls = s.numberOfControls;
	desc.baseControl = s.baseControl;
	desc.numberOfSignalSelectors = s.numberOfSignalSelectors;
	desc.baseSignalSelector = s.baseSignalSelector;
	desc.numberOfMixers = s.numberOfMixers;
	desc.baseMixer = s.baseMixer;
	desc.numberOfMatrices = s.numberOfMatrices;
	desc.baseMatrix = s.baseMatrix;
	desc.numberOfSplitters = s.numberOfSplitters;
	desc.baseSplitter = s.baseSplitter;
	desc.numberOfCombiners = s.numberOfCombiners;
	desc.baseCombiner = s.baseCombiner;
	desc.numberOfDemultiplexers = s.numberOfDemultiplexers;
	desc.baseDemultiplexer = s.baseDemultiplexer;
	desc.numberOfMultiplexers = s.numberOfMultiplexers;
	desc.baseMultiplexer = s.baseMultiplexer;
	desc.numberOfTranscoders = s.numberOfTranscoders;
	desc.baseTranscoder = s.baseTranscoder;
	desc.numberOfControlBlocks = s.numberOfControlBlocks;
	desc.baseControlBlock = s.baseControlBlock;
	desc.currentSamplingRate = d.currentSamplingRate;
	desc.samplingRates = s.samplingRates;
	return desc;
}

StreamDescriptor AemHandler::buildStreamOutputDescriptor(ConfigurationIndex const configIndex, StreamIndex const streamIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.streamOutputModels.find(streamIndex);
	if (it == cfg.streamOutputModels.end())
	{
		throw NoSuchDescriptorException{};
	}
	return makeStreamDescriptor(it->second.staticModel, it->second.dynamicModel);
}

StreamDescriptor AemHandler::buildStreamInputDescriptor(ConfigurationIndex const configIndex, StreamIndex const streamIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.streamInputModels.find(streamIndex);
	if (it == cfg.streamInputModels.end())
	{
		throw NoSuchDescriptorException{};
	}
	return makeStreamDescriptor(it->second.staticModel, it->second.dynamicModel);
}

AvbInterfaceDescriptor AemHandler::buildAvbInterfaceDescriptor(ConfigurationIndex const configIndex, AvbInterfaceIndex const avbInterfaceIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.avbInterfaceModels.find(avbInterfaceIndex);
	if (it == cfg.avbInterfaceModels.end())
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = it->second.staticModel;
	auto const& d = it->second.dynamicModel;
	auto desc = AvbInterfaceDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.macAddress = d.macAddress;
	desc.interfaceFlags = s.interfaceFlags;
	desc.clockIdentity = d.clockIdentity;
	desc.priority1 = d.priority1;
	desc.clockClass = d.clockClass;
	desc.offsetScaledLogVariance = d.offsetScaledLogVariance;
	desc.clockAccuracy = d.clockAccuracy;
	desc.priority2 = d.priority2;
	desc.domainNumber = d.domainNumber;
	desc.logSyncInterval = d.logSyncInterval;
	desc.logAnnounceInterval = d.logAnnounceInterval;
	desc.logPDelayInterval = d.logPDelayInterval;
	desc.portNumber = s.portNumber;
	return desc;
}

ClockSourceDescriptor AemHandler::buildClockSourceDescriptor(ConfigurationIndex const configIndex, ClockSourceIndex const clockSourceIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.clockSourceModels.find(clockSourceIndex);
	if (it == cfg.clockSourceModels.end())
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = it->second.staticModel;
	auto const& d = it->second.dynamicModel;
	auto desc = ClockSourceDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.clockSourceFlags = d.clockSourceFlags;
	desc.clockSourceType = s.clockSourceType;
	desc.clockSourceIdentifier = d.clockSourceIdentifier;
	desc.clockSourceLocationType = s.clockSourceLocationType;
	desc.clockSourceLocationIndex = s.clockSourceLocationIndex;
	return desc;
}

ClockDomainDescriptor AemHandler::buildClockDomainDescriptor(ConfigurationIndex const configIndex, ClockDomainIndex const clockDomainIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.clockDomainModels.find(clockDomainIndex);
	if (it == cfg.clockDomainModels.end())
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = it->second.staticModel;
	auto const& d = it->second.dynamicModel;
	auto desc = ClockDomainDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.clockSourceIndex = d.clockSourceIndex;
	desc.clockSources = s.clockSources;
	return desc;
}

LocaleDescriptor AemHandler::buildLocaleDescriptor(ConfigurationIndex const configIndex, LocaleIndex const localeIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const it = cfg.localeTrees.find(localeIndex);
	if (it == cfg.localeTrees.end())
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = it->second.staticModel;
	auto desc = LocaleDescriptor{};
	desc.localeID = s.localeID;
	desc.numberOfStringDescriptors = s.numberOfStringDescriptors;
	desc.baseStringDescriptorIndex = s.baseStringDescriptorIndex;
	return desc;
}

StringsDescriptor AemHandler::buildStringsDescriptor(ConfigurationIndex const configIndex, StringsIndex const stringsIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	for (auto const& [localeIndex, locale] : cfg.localeTrees)
	{
		auto const it = locale.stringsModels.find(stringsIndex);
		if (it != locale.stringsModels.end())
		{
			auto desc = StringsDescriptor{};
			desc.strings = it->second.staticModel.strings;
			return desc;
		}
	}
	throw NoSuchDescriptorException{};
}

StreamPortDescriptor AemHandler::buildStreamPortOutputDescriptor(ConfigurationIndex const configIndex, StreamPortIndex const streamPortIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const* const streamPort = findStreamPortOutput(cfg, streamPortIndex);
	if (streamPort == nullptr)
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = streamPort->staticModel;
	auto desc = StreamPortDescriptor{};
	desc.clockDomainIndex = s.clockDomainIndex;
	desc.portFlags = s.portFlags;
	desc.numberOfControls = s.numberOfControls;
	desc.baseControl = s.baseControl;
	desc.numberOfClusters = s.numberOfClusters;
	desc.baseCluster = s.baseCluster;
	desc.numberOfMaps = s.numberOfMaps;
	desc.baseMap = s.baseMap;
	return desc;
}

AudioClusterDescriptor AemHandler::buildAudioClusterDescriptor(ConfigurationIndex const configIndex, ClusterIndex const clusterIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const* const cluster = findAudioCluster(cfg, clusterIndex);
	if (cluster == nullptr)
	{
		throw NoSuchDescriptorException{};
	}
	auto const& s = cluster->staticModel;
	auto const& d = cluster->dynamicModel;
	auto desc = AudioClusterDescriptor{};
	desc.objectName = d.objectName;
	desc.localizedDescription = s.localizedDescription;
	desc.signalType = s.signalType;
	desc.signalIndex = s.signalIndex;
	desc.signalOutput = s.signalOutput;
	desc.pathLatency = s.pathLatency;
	desc.blockLatency = s.blockLatency;
	desc.channelCount = s.channelCount;
	desc.format = s.format;
	return desc;
}

AudioMapDescriptor AemHandler::buildAudioMapDescriptor(ConfigurationIndex const configIndex, MapIndex const mapIndex) const
{
	auto const& cfg = getConfigurationTree(_entityModelTree, configIndex);
	auto const* const map = findAudioMap(cfg, mapIndex);
	if (map == nullptr)
	{
		throw NoSuchDescriptorException{};
	}
	auto desc = AudioMapDescriptor{};
	desc.mappings = map->staticModel.mappings;
	return desc;
}

} // namespace model
} // namespace entity
} // namespace avdecc
} // namespace la
