#include "LogicalFontSharding.h"
#include "LogicalType0Font.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		constexpr std::uint32_t MakeTag(char a, char b, char c, char d)
		{
			return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
			       static_cast<std::uint32_t>(static_cast<unsigned char>(d));
		}

		std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) |
			                                  static_cast<std::uint16_t>(data[offset + 1]));
		}

		std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return (static_cast<std::uint32_t>(data[offset]) << 24) |
			       (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
			       (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
			       static_cast<std::uint32_t>(data[offset + 3]);
		}

		void WriteU16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value)
		{
			data[offset] = static_cast<std::uint8_t>(value >> 8);
			data[offset + 1] = static_cast<std::uint8_t>(value);
		}

		void WriteU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
		{
			data[offset] = static_cast<std::uint8_t>(value >> 24);
			data[offset + 1] = static_cast<std::uint8_t>(value >> 16);
			data[offset + 2] = static_cast<std::uint8_t>(value >> 8);
			data[offset + 3] = static_cast<std::uint8_t>(value);
		}

		void AppendU16(std::vector<std::uint8_t>& data, std::uint16_t value)
		{
			data.push_back(static_cast<std::uint8_t>(value >> 8));
			data.push_back(static_cast<std::uint8_t>(value));
		}

		void AppendU32(std::vector<std::uint8_t>& data, std::uint32_t value)
		{
			data.push_back(static_cast<std::uint8_t>(value >> 24));
			data.push_back(static_cast<std::uint8_t>(value >> 16));
			data.push_back(static_cast<std::uint8_t>(value >> 8));
			data.push_back(static_cast<std::uint8_t>(value));
		}

		std::vector<std::uint8_t> MakeSimpleGlyph()
		{
			return std::vector<std::uint8_t>(12, 0);
		}

		std::vector<std::uint8_t> MakeCompositeGlyph(const std::vector<std::uint16_t>& components)
		{
			std::vector<std::uint8_t> glyph(10, 0);
			WriteU16(glyph, 0, 0xFFFFu);
			for (std::size_t index = 0; index < components.size(); ++index)
			{
				std::uint16_t flags = 0x0003u;
				if (index + 1 < components.size())
					flags |= 0x0020u;
				AppendU16(glyph, flags);
				AppendU16(glyph, components[index]);
				AppendU16(glyph, 0);
				AppendU16(glyph, 0);
			}
			return glyph;
		}

		std::vector<std::uint8_t> BuildTestFont(
			std::uint16_t numGlyphs,
			const std::map<std::uint16_t, std::vector<std::uint16_t>>& composites = {})
		{
			std::vector<std::uint8_t> glyf;
			std::vector<std::uint8_t> loca;
			std::vector<std::uint8_t> hmtx;
			for (std::uint32_t gid = 0; gid < numGlyphs; ++gid)
			{
				AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));
				const auto composite = composites.find(static_cast<std::uint16_t>(gid));
				const std::vector<std::uint8_t> glyph = composite == composites.end()
				                                               ? MakeSimpleGlyph()
				                                               : MakeCompositeGlyph(composite->second);
				glyf.insert(glyf.end(), glyph.begin(), glyph.end());
				AppendU16(hmtx, static_cast<std::uint16_t>(500 + gid % 100));
				AppendU16(hmtx, 0);
			}
			AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));

			std::vector<std::uint8_t> head(54, 0);
			WriteU32(head, 0, 0x00010000u);
			WriteU32(head, 12, 0x5F0F3CF5u);
			WriteU16(head, 18, 1000);
			WriteU16(head, 50, 1);
			std::vector<std::uint8_t> hhea(36, 0);
			WriteU32(hhea, 0, 0x00010000u);
			WriteU16(hhea, 34, numGlyphs);
			std::vector<std::uint8_t> maxp(32, 0);
			WriteU32(maxp, 0, 0x00010000u);
			WriteU16(maxp, 4, numGlyphs);

			std::map<std::uint32_t, std::vector<std::uint8_t>> tables;
			tables.emplace(MakeTag('g', 'l', 'y', 'f'), std::move(glyf));
			tables.emplace(MakeTag('h', 'e', 'a', 'd'), std::move(head));
			tables.emplace(MakeTag('h', 'h', 'e', 'a'), std::move(hhea));
			tables.emplace(MakeTag('h', 'm', 't', 'x'), std::move(hmtx));
			tables.emplace(MakeTag('l', 'o', 'c', 'a'), std::move(loca));
			tables.emplace(MakeTag('m', 'a', 'x', 'p'), std::move(maxp));

			const std::size_t directorySize = 12 + tables.size() * 16;
			std::size_t size = directorySize;
			for (const auto& table : tables)
				size += (table.second.size() + 3) & ~static_cast<std::size_t>(3);
			std::vector<std::uint8_t> font(size, 0);
			WriteU32(font, 0, 0x00010000u);
			WriteU16(font, 4, static_cast<std::uint16_t>(tables.size()));
			std::size_t record = 12;
			std::size_t offset = directorySize;
			for (const auto& table : tables)
			{
				WriteU32(font, record, table.first);
				WriteU32(font, record + 8, static_cast<std::uint32_t>(offset));
				WriteU32(font, record + 12, static_cast<std::uint32_t>(table.second.size()));
				std::copy(table.second.begin(), table.second.end(),
				          font.begin() + static_cast<std::ptrdiff_t>(offset));
				record += 16;
				offset += (table.second.size() + 3) & ~static_cast<std::size_t>(3);
			}
			return font;
		}

		int SourceAdvance(std::uint32_t glyphId)
		{
			return static_cast<int>(500 + glyphId % 100);
		}

		CLogicalUnitPlan SourcePlan(const std::u32string& text, std::uint32_t glyphId)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = SourceAdvance(glyphId);
			plan.Visual.Components.push_back({glyphId, 0, 0});
			return plan;
		}

		CLogicalUnitPlan SyntheticPlan(const std::u32string& text, int offset = 0)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = 900;
			plan.Visual.Components.push_back({1, offset, 0});
			plan.Visual.Components.push_back({2, 100 + offset, 0});
			return plan;
		}

		CShardedLogicalFontMapping Map(CShardedLogicalFontMapper& mapper,
		                               const CLogicalUnitPlan& plan)
		{
			CShardedLogicalFontMapping mapping;
			CLogicalFontShardingError error;
			EXPECT_TRUE(mapper.TryMap(plan, mapping, error)) << error.Message;
			return mapping;
		}

		std::uint16_t ReadNumGlyphs(const std::vector<std::uint8_t>& font)
		{
			const std::uint16_t tableCount = ReadU16(font, 4);
			for (std::uint16_t index = 0; index < tableCount; ++index)
			{
				const std::size_t record = 12 + static_cast<std::size_t>(index) * 16;
				if (ReadU32(font, record) == MakeTag('m', 'a', 'x', 'p'))
					return ReadU16(font, ReadU32(font, record + 8) + 4);
			}
			return 0;
		}
	}

	TEST(LogicalFontSharding, ShardsOnSemanticCapacityWithoutDuplicatingAVisualWithinAShard)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(4), 2, 2);
		const CShardedLogicalFontMapping first = Map(mapper, SourcePlan(U"A", 1));
		const CShardedLogicalFontMapping second = Map(mapper, SourcePlan(U"B", 1));
		const CShardedLogicalFontMapping third = Map(mapper, SourcePlan(U"C", 1));

		EXPECT_EQ(0u, first.ShardIndex);
		EXPECT_EQ(0u, second.ShardIndex);
		EXPECT_EQ(1u, third.ShardIndex);
		EXPECT_EQ(1u, first.FontMapping.Cid);
		EXPECT_EQ(2u, second.FontMapping.Cid);
		EXPECT_EQ(1u, third.FontMapping.Cid);
		EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(0));
		EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(1));
	}

	TEST(LogicalFontSharding, ShardsOnEmbeddedCapacityIndependentlyOfSemanticCapacity)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(5), 10, 3);
		Map(mapper, SourcePlan(U"A", 1));
		Map(mapper, SourcePlan(U"B", 2));
		const CShardedLogicalFontMapping third = Map(mapper, SourcePlan(U"C", 3));

		ASSERT_EQ(2u, mapper.GetShardCount());
		EXPECT_EQ(3u, mapper.GetEmbeddedGlyphCount(0));
		EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(1));
		EXPECT_EQ(1u, third.ShardIndex);
		EXPECT_EQ(1u, third.FontMapping.Cid);
	}

	TEST(LogicalFontSharding, ReusesRepeatedSemanticOnItsOriginalShardWithoutCapacityCost)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(4), 1, 2);
		const CLogicalUnitPlan original = SourcePlan(U"A", 1);
		const CShardedLogicalFontMapping first = Map(mapper, original);
		Map(mapper, SourcePlan(U"B", 2));
		const CShardedLogicalFontMapping repeated = Map(mapper, original);

		ASSERT_EQ(2u, mapper.GetShardCount());
		EXPECT_EQ(first.ShardIndex, repeated.ShardIndex);
		EXPECT_EQ(first.FontMapping.Cid, repeated.FontMapping.Cid);
		EXPECT_FALSE(repeated.FontMapping.SemanticCreated);
		EXPECT_FALSE(repeated.FontMapping.VisualCreated);
		EXPECT_EQ(1u, mapper.GetShard(0)->GetSemanticCount());
		EXPECT_EQ(1u, mapper.GetShard(1)->GetSemanticCount());
	}

	TEST(LogicalFontSharding, CountsSharedCompositeDependenciesOnlyOnce)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(6, {{3, {1, 2}}, {4, {3}}});
		CShardedLogicalFontMapper mapper(source, 10, 5);
		Map(mapper, SourcePlan(U"A", 3));
		EXPECT_EQ(4u, mapper.GetEmbeddedGlyphCount(0));
		Map(mapper, SourcePlan(U"B", 4));
		EXPECT_EQ(5u, mapper.GetEmbeddedGlyphCount(0));
		const CShardedLogicalFontMapping next = Map(mapper, SourcePlan(U"C", 5));
		EXPECT_EQ(1u, next.ShardIndex);
		EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(1));
	}

	TEST(LogicalFontSharding, CountsTransitiveCompositeClosureExactly)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, {{3, {1, 2}}, {4, {3}}});
		CShardedLogicalFontMapper tooSmall(source, 10, 4);
		CShardedLogicalFontMapping unchanged;
		unchanged.ShardIndex = 99;
		unchanged.FontMapping.Cid = 77;
		CLogicalFontShardingError error;
		EXPECT_FALSE(tooSmall.TryMap(SourcePlan(U"A", 4), unchanged, error));
		EXPECT_EQ(CLogicalFontShardingErrorCode::VisualExceedsCapacity, error.Code);
		EXPECT_EQ(99u, unchanged.ShardIndex);
		EXPECT_EQ(77u, unchanged.FontMapping.Cid);
		EXPECT_EQ(0u, tooSmall.GetShardCount());

		CShardedLogicalFontMapper exact(source, 10, 5);
		Map(exact, SourcePlan(U"A", 4));
		EXPECT_EQ(5u, exact.GetEmbeddedGlyphCount(0));
	}

	TEST(LogicalFontSharding, CompactsAHighSourceGidWithoutChargingForUnusedGlyphs)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(2000), 10, 2);
		Map(mapper, SourcePlan(U"A", 1999));
		EXPECT_EQ(1u, mapper.GetShardCount());
		EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(0));
	}

	TEST(LogicalFontSharding, ChargesOneEmbeddedGidPerUniqueSyntheticVisual)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(4), 10, 4);
		const CShardedLogicalFontMapping first = Map(mapper, SyntheticPlan(U"A"));
		const CShardedLogicalFontMapping sameVisual = Map(mapper, SyntheticPlan(U"B"));
		const CShardedLogicalFontMapping nextVisual = Map(mapper, SyntheticPlan(U"C", 1));

		EXPECT_EQ(0u, first.ShardIndex);
		EXPECT_EQ(0u, sameVisual.ShardIndex);
		EXPECT_EQ(first.FontMapping.VisualRecordId, sameVisual.FontMapping.VisualRecordId);
		EXPECT_FALSE(sameVisual.FontMapping.VisualCreated);
		EXPECT_EQ(4u, mapper.GetEmbeddedGlyphCount(0));
		EXPECT_EQ(1u, nextVisual.ShardIndex);
		EXPECT_EQ(4u, mapper.GetEmbeddedGlyphCount(1));
	}

	TEST(LogicalFontSharding, ExplicitlyRejectsAVisualThatCannotFitAnEmptyShard)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(4), 10, 3);
		CShardedLogicalFontMapping mapping;
		mapping.ShardIndex = 42;
		CLogicalFontShardingError error;
		EXPECT_FALSE(mapper.TryMap(SyntheticPlan(U"A"), mapping, error));
		EXPECT_EQ(CLogicalFontShardingErrorCode::VisualExceedsCapacity, error.Code);
		EXPECT_EQ(42u, mapping.ShardIndex);
		EXPECT_EQ(0u, mapper.GetShardCount());
	}

	TEST(LogicalFontSharding, ReservesCidZeroAndEmbeddedGidZeroInEveryShard)
	{
		CShardedLogicalFontMapper mapper(BuildTestFont(4), 1, 2);
		Map(mapper, SourcePlan(U"A", 1));
		Map(mapper, SourcePlan(U"B", 2));

		ASSERT_EQ(2u, mapper.GetShardCount());
		for (std::size_t index = 0; index < mapper.GetShardCount(); ++index)
		{
			ASSERT_NE(nullptr, mapper.GetShard(index));
			EXPECT_EQ(nullptr, mapper.GetShard(index)->GetCidRecord(0));
			EXPECT_EQ(nullptr, mapper.GetShard(index)->GetVisualRecord(0));
			EXPECT_EQ(1u, mapper.GetShard(index)->GetCidRecord(1)->Cid);
			EXPECT_EQ(2u, mapper.GetEmbeddedGlyphCount(index));
		}
	}

	TEST(LogicalFontSharding, RejectsZeroCapacityWithoutAllocationOrResultMutation)
	{
		for (const std::pair<std::size_t, std::size_t> capacities :
		     {std::make_pair(0u, 2u), std::make_pair(2u, 0u)})
		{
			CShardedLogicalFontMapper mapper(BuildTestFont(3), capacities.first, capacities.second);
			CShardedLogicalFontMapping mapping;
			mapping.ShardIndex = 12;
			mapping.FontMapping.Cid = 34;
			CLogicalFontShardingError error;
			EXPECT_FALSE(mapper.TryMap(SourcePlan(U"A", 1), mapping, error));
			EXPECT_EQ(CLogicalFontShardingErrorCode::InvalidCapacity, error.Code);
			EXPECT_EQ(12u, mapping.ShardIndex);
			EXPECT_EQ(34u, mapping.FontMapping.Cid);
			EXPECT_EQ(0u, mapper.GetShardCount());
		}
	}

	TEST(LogicalFontSharding, ClampsOversizedSemanticCapacityWithoutWrappingCidAllocation)
	{
		const std::size_t oversized = CShardedLogicalFontMapper::PhysicalLimit + 100;
		CShardedLogicalFontMapper mapper(BuildTestFont(3), oversized, 2);
		CShardedLogicalFontMapping last;
		for (std::uint32_t index = 0; index <= std::numeric_limits<std::uint16_t>::max(); ++index)
		{
			CLogicalUnitPlan plan = SourcePlan(std::u32string{U'A', static_cast<char32_t>(index)}, 1);
			last = Map(mapper, plan);
		}

		ASSERT_EQ(2u, mapper.GetShardCount());
		EXPECT_EQ(CShardedLogicalFontMapper::PhysicalLimit, mapper.GetShard(0)->GetSemanticCount());
		EXPECT_EQ(1u, mapper.GetShard(1)->GetSemanticCount());
		EXPECT_EQ(1u, last.ShardIndex);
		EXPECT_EQ(1u, last.FontMapping.Cid);
	}

	TEST(LogicalFontSharding, SerializedShardGlyphCountsMatchTheCapacityTracker)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(6, {{3, {1, 2}}});
		CShardedLogicalFontMapper mapper(source, 2, 6);
		Map(mapper, SourcePlan(U"A", 3));
		Map(mapper, SourcePlan(U"B", 3));
		Map(mapper, SyntheticPlan(U"C"));
		Map(mapper, SourcePlan(U"D", 5));

		ASSERT_EQ(2u, mapper.GetShardCount());
		for (std::size_t index = 0; index < mapper.GetShardCount(); ++index)
		{
			CLogicalType0FontResult result;
			CLogicalType0FontError error;
			ASSERT_TRUE(TryBuildLogicalType0Font(source, *mapper.GetShard(index), result, error))
				<< error.Message;
			EXPECT_EQ(mapper.GetEmbeddedGlyphCount(index), ReadNumGlyphs(result.FontFile2));
			const std::string fileName = "phase5-shard-" + std::to_string(index) + ".ttf";
			std::ofstream stream(fileName, std::ios::binary);
			ASSERT_TRUE(stream.good());
			stream.write(reinterpret_cast<const char*>(result.FontFile2.data()),
			             static_cast<std::streamsize>(result.FontFile2.size()));
			ASSERT_TRUE(stream.good());
		}
	}
}
