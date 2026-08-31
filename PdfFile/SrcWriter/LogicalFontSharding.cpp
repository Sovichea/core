/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation.
 */
#include "LogicalFontSharding.h"

#include <algorithm>
#include <utility>

namespace PdfWriter
{
	namespace
	{
		bool SetError(CLogicalFontShardingError& error,
		              CLogicalFontShardingErrorCode code,
		              const std::string& message)
		{
			error.Code = code;
			error.Message = message;
			return false;
		}
	}

	CShardedLogicalFontMapper::CShardState::CShardState(
		const std::shared_ptr<const std::vector<std::uint8_t>>& source)
		: Glyphs(source)
	{
	}

	CShardedLogicalFontMapper::CShardedLogicalFontMapper(
		std::vector<std::uint8_t> source,
		std::size_t semanticCapacity,
		std::size_t embeddedGlyphCapacity)
		: m_source(std::make_shared<const std::vector<std::uint8_t>>(std::move(source))),
		  m_semanticCapacity(std::min(semanticCapacity, PhysicalLimit)),
		  m_embeddedGlyphCapacity(std::min(embeddedGlyphCapacity, PhysicalLimit))
	{
	}

	bool CShardedLogicalFontMapper::TryPlanForShard(
		const CLogicalUnitPlan& plan,
		CShardState& shard,
		CLogicalCompactGlyphAddition& addition,
		bool& visualCreated,
		CLogicalFontShardingError& error) const
	{
		if (shard.Font.GetSemanticCount() >= m_semanticCapacity)
			return false;

		visualCreated = shard.Font.GetVisualRecordId(plan.Visual) == 0;
		addition = CLogicalCompactGlyphAddition();
		if (!visualCreated)
			return true;

		CLogicalTrueTypeSubsetError glyphError;
		if (!shard.Glyphs.TryPlan(plan.Visual, addition, glyphError))
		{
			error.GlyphError = glyphError;
			return SetError(error, CLogicalFontShardingErrorCode::GlyphPlanningFailed,
			                "failed to plan compact glyph capacity: " + glyphError.Message);
		}
		return shard.Glyphs.CanCommit(addition, m_embeddedGlyphCapacity);
	}

	bool CShardedLogicalFontMapper::TryCreateShard(
		const CLogicalUnitPlan& plan,
		CShardedLogicalFontMapping& mapping,
		CLogicalFontShardingError& error)
	{
		CShardState candidate(m_source);
		CLogicalCompactGlyphAddition addition;
		bool visualCreated = false;
		if (!TryPlanForShard(plan, candidate, addition, visualCreated, error))
		{
			if (error.Code != CLogicalFontShardingErrorCode::None)
				return false;
			return SetError(error, CLogicalFontShardingErrorCode::VisualExceedsCapacity,
			                "one logical visual and its source dependencies exceed an empty shard");
		}

		candidate.Glyphs.Commit(addition);
		mapping.ShardIndex = m_shards.size();
		mapping.FontMapping = candidate.Font.Map(plan);
		m_shards.push_back(std::move(candidate));
		return true;
	}

	bool CShardedLogicalFontMapper::TryMap(const CLogicalUnitPlan& plan,
	                                     CShardedLogicalFontMapping& mapping,
	                                     CLogicalFontShardingError& error)
	{
		error = CLogicalFontShardingError();
		if (m_semanticCapacity == 0 || m_embeddedGlyphCapacity == 0)
			return SetError(error, CLogicalFontShardingErrorCode::InvalidCapacity,
			                "logical font shard capacities must be greater than zero");

		const CSemanticUnitKey semanticKey = plan.GetSemanticKey();
		const auto existing = m_semanticMappings.find(semanticKey);
		if (existing != m_semanticMappings.end())
		{
			CShardedLogicalFontMapping reused = existing->second;
			reused.FontMapping.SemanticCreated = false;
			reused.FontMapping.VisualCreated = false;
			mapping = reused;
			return true;
		}

		CShardedLogicalFontMapping created;
		if (!m_shards.empty())
		{
			CShardState& shard = m_shards.back();
			CLogicalCompactGlyphAddition addition;
			bool visualCreated = false;
			if (TryPlanForShard(plan, shard, addition, visualCreated, error))
			{
				if (visualCreated)
					shard.Glyphs.Commit(addition);
				created.ShardIndex = m_shards.size() - 1;
				created.FontMapping = shard.Font.Map(plan);
			}
			else if (error.Code != CLogicalFontShardingErrorCode::None)
			{
				return false;
			}
			else if (!TryCreateShard(plan, created, error))
			{
				return false;
			}
		}
		else if (!TryCreateShard(plan, created, error))
		{
			return false;
		}

		m_semanticMappings.emplace(semanticKey, created);
		mapping = created;
		return true;
	}

	std::size_t CShardedLogicalFontMapper::GetShardCount() const
	{
		return m_shards.size();
	}

	const CLogicalFontShard* CShardedLogicalFontMapper::GetShard(std::size_t index) const
	{
		return index < m_shards.size() ? &m_shards[index].Font : nullptr;
	}

	std::size_t CShardedLogicalFontMapper::GetEmbeddedGlyphCount(std::size_t index) const
	{
		return index < m_shards.size() ? m_shards[index].Glyphs.GetGlyphCount() : 0;
	}
}
