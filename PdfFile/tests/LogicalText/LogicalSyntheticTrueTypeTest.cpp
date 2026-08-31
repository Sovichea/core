#include "LogicalTrueTypeSubset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
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

		std::int16_t ReadS16(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::int16_t>(ReadU16(data, offset));
		}

		std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return (static_cast<std::uint32_t>(data[offset]) << 24) |
			       (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
			       (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
			       static_cast<std::uint32_t>(data[offset + 3]);
		}

		std::vector<std::uint8_t> ReadSourceFont()
		{
			const std::string path = std::string(EO_CORE_ROOT_DIR) +
				"/DesktopEditor/freetype-2.10.4/docs/reference/assets/fonts/specimen/FontAwesome.ttf";
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return {};
			return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
			                                 std::istreambuf_iterator<char>());
		}

		std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> ReadTables(
			const std::vector<std::uint8_t>& font)
		{
			std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> tables;
			const std::uint16_t count = ReadU16(font, 4);
			for (std::uint16_t index = 0; index < count; ++index)
			{
				const std::size_t record = 12 + static_cast<std::size_t>(index) * 16;
				tables.emplace(ReadU32(font, record),
				               std::make_pair(ReadU32(font, record + 8), ReadU32(font, record + 12)));
			}
			return tables;
		}

		std::uint16_t GetAdvance(const std::vector<std::uint8_t>& source, std::uint32_t glyphId)
		{
			std::uint16_t advance = 0;
			CLogicalTrueTypeSubsetError error;
			EXPECT_TRUE(TryGetTrueTypeGlyphAdvance(source, glyphId, advance, error)) << error.Message;
			return advance;
		}

		CLogicalFontMapping MapVisual(CLogicalFontMapper& mapper,
		                              const std::u32string& text,
		                              int advance,
		                              std::vector<CLogicalComponent> components)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = advance;
			plan.Visual.Components = std::move(components);
			return mapper.Map(plan);
		}

		CLogicalTrueTypeSubsetResult BuildLogical(const std::vector<std::uint8_t>& source,
		                                               const CLogicalFontShard& shard)
		{
			CLogicalTrueTypeSubsetResult result;
			CLogicalTrueTypeSubsetError error;
			EXPECT_TRUE(TryBuildLogicalTrueType(source, shard, result, error)) << error.Message;
			return result;
		}

		std::size_t GlyphOffset(const CLogicalTrueTypeSubsetResult& result,
		                        const std::map<std::uint32_t, std::pair<std::size_t, std::size_t>>& tables,
		                        std::uint16_t gid)
		{
			const std::size_t loca = tables.at(MakeTag('l', 'o', 'c', 'a')).first;
			const std::size_t glyf = tables.at(MakeTag('g', 'l', 'y', 'f')).first;
			return glyf + ReadU32(result.FontData, loca + static_cast<std::size_t>(gid) * 4);
		}
	}

	TEST(LogicalSyntheticTrueType, AppendsSyntheticGlyphForChangedAdvance)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const std::uint32_t sourceGid = 13;
		const int changedAdvance = GetAdvance(source, sourceGid) - 1;
		CLogicalFontMapper mapper;
		MapVisual(mapper, U"A", changedAdvance, {{sourceGid, 0, 0}});

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		ASSERT_EQ(2u, result.VisualRecordToSubsetGid[1]);
		ASSERT_TRUE(result.VisualRecordIsSynthetic[1]);
		const auto tables = ReadTables(result.FontData);
		EXPECT_EQ(3u, ReadU16(result.FontData, tables.at(MakeTag('m', 'a', 'x', 'p')).first + 4));
		const std::size_t hmtx = tables.at(MakeTag('h', 'm', 't', 'x')).first;
		EXPECT_EQ(changedAdvance, ReadU16(result.FontData, hmtx + 8));
		const std::size_t glyph = GlyphOffset(result, tables, 2);
		EXPECT_EQ(-1, ReadS16(result.FontData, glyph));
		EXPECT_EQ(0x0003u, ReadU16(result.FontData, glyph + 10));
		EXPECT_EQ(1u, ReadU16(result.FontData, glyph + 12));
		EXPECT_EQ(0, ReadS16(result.FontData, glyph + 14));
		EXPECT_EQ(0, ReadS16(result.FontData, glyph + 16));
	}

	TEST(LogicalSyntheticTrueType, PreservesPositionedComponentAndTranslatedBounds)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const std::uint32_t sourceGid = 13;
		CLogicalFontMapper mapper;
		MapVisual(mapper, U"A", GetAdvance(source, sourceGid), {{sourceGid, 120, -45}});

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		const auto tables = ReadTables(result.FontData);
		const std::size_t glyph = GlyphOffset(result, tables, 2);
		EXPECT_EQ(120, ReadS16(result.FontData, glyph + 14));
		EXPECT_EQ(-45, ReadS16(result.FontData, glyph + 16));
		EXPECT_EQ(ReadS16(result.FontData, glyph + 2),
		          ReadS16(result.FontData, tables.at(MakeTag('h', 'm', 't', 'x')).first + 10));
		EXPECT_LE(ReadS16(result.FontData, tables.at(MakeTag('h', 'e', 'a', 'd')).first + 36),
		          ReadS16(result.FontData, glyph + 2));
		EXPECT_GE(ReadS16(result.FontData, tables.at(MakeTag('h', 'e', 'a', 'd')).first + 40),
		          ReadS16(result.FontData, glyph + 6));
	}

	TEST(LogicalSyntheticTrueType, BuildsOrderedMultiComponentComposite)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalFontMapper mapper;
		MapVisual(mapper, U"AB", 1700, {{13, 0, 0}, {14, 220, 35}});

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		ASSERT_EQ(3u, result.VisualRecordToSubsetGid[1]);
		const auto tables = ReadTables(result.FontData);
		const std::size_t glyph = GlyphOffset(result, tables, 3);
		EXPECT_EQ(0x0023u, ReadU16(result.FontData, glyph + 10));
		EXPECT_EQ(result.SourceGidToSubsetGid[13], ReadU16(result.FontData, glyph + 12));
		EXPECT_EQ(0, ReadS16(result.FontData, glyph + 14));
		EXPECT_EQ(0, ReadS16(result.FontData, glyph + 16));
		EXPECT_EQ(0x0003u, ReadU16(result.FontData, glyph + 18));
		EXPECT_EQ(result.SourceGidToSubsetGid[14], ReadU16(result.FontData, glyph + 20));
		EXPECT_EQ(220, ReadS16(result.FontData, glyph + 22));
		EXPECT_EQ(35, ReadS16(result.FontData, glyph + 24));
		EXPECT_GE(ReadU16(result.FontData, tables.at(MakeTag('m', 'a', 'x', 'p')).first + 28), 2u);
		std::ofstream stream("phase4-synthetic.ttf", std::ios::binary);
		ASSERT_TRUE(stream.good());
		stream.write(reinterpret_cast<const char*>(result.FontData.data()),
		             static_cast<std::streamsize>(result.FontData.size()));
		ASSERT_TRUE(stream.good());
	}

	TEST(LogicalSyntheticTrueType, SharesSyntheticVisualAcrossSemanticCids)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = MapVisual(mapper, U"A", 1700, {{13, 0, 0}, {14, 20, 0}});
		const CLogicalFontMapping second = MapVisual(mapper, U"B", 1700, {{13, 0, 0}, {14, 20, 0}});
		ASSERT_NE(first.Cid, second.Cid);
		ASSERT_EQ(first.VisualRecordId, second.VisualRecordId);

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		EXPECT_EQ(result.CidToSubsetGid[first.Cid], result.CidToSubsetGid[second.Cid]);
		EXPECT_TRUE(result.VisualRecordIsSynthetic[first.VisualRecordId]);
	}

	TEST(LogicalSyntheticTrueType, AllocatesDistinctSyntheticVisuals)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = MapVisual(mapper, U"A", 1700, {{13, 0, 0}, {14, 20, 0}});
		const CLogicalFontMapping second = MapVisual(mapper, U"A", 1700, {{13, 0, 0}, {14, 21, 0}});

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		EXPECT_NE(result.CidToSubsetGid[first.Cid], result.CidToSubsetGid[second.Cid]);
	}

	TEST(LogicalSyntheticTrueType, IgnoresContourlessComponentsWhenCalculatingBounds)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());

		CLogicalFontMapper emptyMapper;
		MapVisual(emptyMapper, U"A", 1000, {{1, 30000, -30000}});
		const CLogicalTrueTypeSubsetResult empty = BuildLogical(source, emptyMapper.GetShard());
		const auto emptyTables = ReadTables(empty.FontData);
		const std::size_t emptyGlyph = GlyphOffset(empty, emptyTables, 2);
		EXPECT_EQ(0, ReadS16(empty.FontData, emptyGlyph + 2));
		EXPECT_EQ(0, ReadS16(empty.FontData, emptyGlyph + 4));
		EXPECT_EQ(0, ReadS16(empty.FontData, emptyGlyph + 6));
		EXPECT_EQ(0, ReadS16(empty.FontData, emptyGlyph + 8));
		EXPECT_EQ(0, ReadS16(empty.FontData,
		                      emptyTables.at(MakeTag('h', 'm', 't', 'x')).first + 10));

		CLogicalFontMapper mixedMapper;
		MapVisual(mixedMapper, U"A", 1800, {{13, 0, 0}, {1, 32767, -32768}});
		const CLogicalTrueTypeSubsetResult mixed = BuildLogical(source, mixedMapper.GetShard());
		const auto mixedTables = ReadTables(mixed.FontData);
		const std::size_t mixedGlyph = GlyphOffset(mixed, mixedTables, 3);
		EXPECT_LT(ReadS16(mixed.FontData, mixedGlyph + 6), 32767);
		EXPECT_GT(ReadS16(mixed.FontData, mixedGlyph + 4), -32768);
	}

	TEST(LogicalSyntheticTrueType, EnforcesDirectComponentCountLimit)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		std::vector<CLogicalComponent> boundary(65535, CLogicalComponent{1, 0, 0});
		CLogicalFontMapper boundaryMapper;
		MapVisual(boundaryMapper, U"A", 1000, std::move(boundary));
		const CLogicalTrueTypeSubsetResult valid = BuildLogical(source, boundaryMapper.GetShard());
		const auto tables = ReadTables(valid.FontData);
		EXPECT_EQ(65535u,
		          ReadU16(valid.FontData, tables.at(MakeTag('m', 'a', 'x', 'p')).first + 28));

		std::vector<CLogicalComponent> overflow(65536, CLogicalComponent{1, 0, 0});
		CLogicalFontMapper overflowMapper;
		MapVisual(overflowMapper, U"A", 1000, std::move(overflow));
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;
		EXPECT_FALSE(TryBuildLogicalTrueType(source, overflowMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::SyntheticResourceOverflow, error.Code);
	}

	TEST(LogicalSyntheticTrueType, AcceptsRepresentableCoordinateAndAdvanceBoundaries)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalFontMapper mapper;
		MapVisual(mapper, U"A", 65535, {{1, 32767, -32768}});

		const CLogicalTrueTypeSubsetResult result = BuildLogical(source, mapper.GetShard());
		EXPECT_TRUE(result.VisualRecordIsSynthetic[1]);
		const auto tables = ReadTables(result.FontData);
		const std::size_t glyph = GlyphOffset(result, tables, 2);
		EXPECT_EQ(32767, ReadS16(result.FontData, glyph + 14));
		EXPECT_EQ(-32768, ReadS16(result.FontData, glyph + 16));
	}

	TEST(LogicalSyntheticTrueType, RejectsUnrepresentableCoordinatesBoundsAndAdvances)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;

		CLogicalFontMapper coordinateMapper;
		MapVisual(coordinateMapper, U"A", 1000, {{1, 32768, 0}});
		EXPECT_FALSE(TryBuildLogicalTrueType(source, coordinateMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::ComponentCoordinateOverflow, error.Code);

		CLogicalFontMapper boundsMapper;
		MapVisual(boundsMapper, U"A", 1000, {{13, 32767, 0}});
		EXPECT_FALSE(TryBuildLogicalTrueType(source, boundsMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::SyntheticBoundsOverflow, error.Code);

		CLogicalFontMapper advanceMapper;
		MapVisual(advanceMapper, U"A", 65536, {{1, 0, 0}});
		EXPECT_FALSE(TryBuildLogicalTrueType(source, advanceMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::InvalidAdvanceWidth, error.Code);
	}
}
