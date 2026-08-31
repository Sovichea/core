/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation.
 */
#ifndef _PDF_WRITER_SRC_LOGICALFONTSHARDING_H
#define _PDF_WRITER_SRC_LOGICALFONTSHARDING_H

#include "LogicalFontMapper.h"
#include "LogicalTrueTypeSubset.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace PdfWriter
{
	struct CShardedLogicalFontMapping
	{
		std::size_t ShardIndex = 0;
		CLogicalFontMapping FontMapping;
	};

	enum class CLogicalFontShardingErrorCode
	{
		None,
		InvalidCapacity,
		GlyphPlanningFailed,
		VisualExceedsCapacity
	};

	struct CLogicalFontShardingError
	{
		CLogicalFontShardingErrorCode Code = CLogicalFontShardingErrorCode::None;
		CLogicalTrueTypeSubsetError GlyphError;
		std::string Message;
	};

	class CShardedLogicalFontMapper
	{
	public:
		static constexpr std::size_t PhysicalLimit = std::numeric_limits<std::uint16_t>::max();

		explicit CShardedLogicalFontMapper(
			std::vector<std::uint8_t> source,
			std::size_t semanticCapacity = PhysicalLimit,
			std::size_t embeddedGlyphCapacity = PhysicalLimit);

		bool TryMap(const CLogicalUnitPlan& plan,
		            CShardedLogicalFontMapping& mapping,
		            CLogicalFontShardingError& error);

		std::size_t GetShardCount() const;
		const CLogicalFontShard* GetShard(std::size_t index) const;
		std::size_t GetEmbeddedGlyphCount(std::size_t index) const;

	private:
		struct CShardState
		{
			explicit CShardState(
				const std::shared_ptr<const std::vector<std::uint8_t>>& source);

			CLogicalFontShard Font;
			CLogicalCompactGlyphTracker Glyphs;
		};

		bool TryPlanForShard(const CLogicalUnitPlan& plan,
		                     CShardState& shard,
		                     CLogicalCompactGlyphAddition& addition,
		                     bool& visualCreated,
		                     CLogicalFontShardingError& error) const;
		bool TryCreateShard(const CLogicalUnitPlan& plan,
		                    CShardedLogicalFontMapping& mapping,
		                    CLogicalFontShardingError& error);

		std::shared_ptr<const std::vector<std::uint8_t>> m_source;
		std::size_t m_semanticCapacity = PhysicalLimit;
		std::size_t m_embeddedGlyphCapacity = PhysicalLimit;
		std::map<CSemanticUnitKey, CShardedLogicalFontMapping> m_semanticMappings;
		std::vector<CShardState> m_shards;
	};
}

#endif
