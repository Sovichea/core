/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * The interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 */
#include "LogicalTrueTypeSubset.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

		constexpr std::uint32_t TagTtcf = MakeTag('t', 't', 'c', 'f');
		constexpr std::uint32_t TagOtto = MakeTag('O', 'T', 'T', 'O');
		constexpr std::uint32_t TagTrue = MakeTag('t', 'r', 'u', 'e');
		constexpr std::uint32_t TagHead = MakeTag('h', 'e', 'a', 'd');
		constexpr std::uint32_t TagHhea = MakeTag('h', 'h', 'e', 'a');
		constexpr std::uint32_t TagMaxp = MakeTag('m', 'a', 'x', 'p');
		constexpr std::uint32_t TagHmtx = MakeTag('h', 'm', 't', 'x');
		constexpr std::uint32_t TagLoca = MakeTag('l', 'o', 'c', 'a');
		constexpr std::uint32_t TagGlyf = MakeTag('g', 'l', 'y', 'f');
		constexpr std::uint32_t TagCmap = MakeTag('c', 'm', 'a', 'p');
		constexpr std::uint32_t TagPost = MakeTag('p', 'o', 's', 't');
		constexpr std::uint32_t SfntTrueType = 0x00010000u;
		constexpr std::uint32_t ChecksumMagic = 0xB1B0AFBAu;
		constexpr std::uint16_t ArgWords = 0x0001u;
		constexpr std::uint16_t MoreComponents = 0x0020u;
		constexpr std::uint16_t HaveScale = 0x0008u;
		constexpr std::uint16_t HaveXyScale = 0x0040u;
		constexpr std::uint16_t HaveTwoByTwo = 0x0080u;
		constexpr std::uint16_t HaveInstructions = 0x0100u;

		struct CTableView
		{
			std::size_t Offset = 0;
			std::size_t Length = 0;
		};

		struct CSourceFont
		{
			std::uint32_t Flavor = 0;
			std::map<std::uint32_t, CTableView> Tables;
			std::uint16_t NumGlyphs = 0;
			std::uint16_t NumberOfHMetrics = 0;
			std::int16_t LocaFormat = 0;
			std::uint16_t UnitsPerEm = 0;
			std::vector<std::uint32_t> GlyphOffsets;
			std::vector<std::uint16_t> Advances;
			std::vector<std::int16_t> LeftSideBearings;
		};

		struct CCompositeReference
		{
			std::uint16_t GlyphId = 0;
			std::size_t GlyphIdOffset = 0;
		};

		struct COutputTable
		{
			std::uint32_t Tag = 0;
			std::vector<std::uint8_t> Data;
			std::uint32_t Checksum = 0;
			std::uint32_t Offset = 0;
		};

		bool SetError(CLogicalTrueTypeSubsetError& error,
		              CLogicalTrueTypeSubsetErrorCode code,
		              const std::string& message,
		              TLogicalVisualRecordId visualRecordId = 0,
		              std::uint32_t sourceGlyphId = CLogicalTrueTypeSubsetError::NoSourceGlyph)
		{
			error.Code = code;
			error.VisualRecordId = visualRecordId;
			error.SourceGlyphId = sourceGlyphId;
			error.Message = message;
			return false;
		}

		bool HasRange(std::size_t size, std::size_t offset, std::size_t length)
		{
			return offset <= size && length <= size - offset;
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

		std::vector<std::uint8_t> CopyTable(const std::vector<std::uint8_t>& source, const CTableView& table)
		{
			return std::vector<std::uint8_t>(source.begin() + static_cast<std::ptrdiff_t>(table.Offset),
			                                 source.begin() + static_cast<std::ptrdiff_t>(table.Offset + table.Length));
		}

		bool ParseSourceFont(const std::vector<std::uint8_t>& data,
		                     CSourceFont& font,
		                     CLogicalTrueTypeSubsetError& error)
		{
			if (!HasRange(data.size(), 0, 4))
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt, "SFNT signature is truncated");

			font.Flavor = ReadU32(data, 0);
			if (font.Flavor == TagTtcf)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::UnsupportedCollection,
				                "TrueType collections are not supported");
			if (font.Flavor == TagOtto)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::UnsupportedFontFlavor,
				                "OpenType CFF fonts are not supported");
			if (font.Flavor != SfntTrueType && font.Flavor != TagTrue)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::UnsupportedFontFlavor,
				                "font is not a standalone TrueType glyf SFNT");
			if (!HasRange(data.size(), 0, 12))
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt, "SFNT header is truncated");

			const std::uint16_t numTables = ReadU16(data, 4);
			if (numTables == 0 || !HasRange(data.size(), 12, static_cast<std::size_t>(numTables) * 16))
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt,
				                "SFNT table directory is truncated or empty");

			font.Tables.clear();
			const std::size_t directoryEnd = 12 + static_cast<std::size_t>(numTables) * 16;
			std::vector<std::pair<std::size_t, std::size_t>> tableRanges;
			for (std::uint16_t i = 0; i < numTables; ++i)
			{
				const std::size_t record = 12 + static_cast<std::size_t>(i) * 16;
				const std::uint32_t tag = ReadU32(data, record);
				const std::uint32_t offset = ReadU32(data, record + 8);
				const std::uint32_t length = ReadU32(data, record + 12);
				if (offset < directoryEnd || (offset & 3u) != 0 || !HasRange(data.size(), offset, length))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt,
					                "SFNT table overlaps the directory, is unaligned, or lies outside the source buffer");
				if (!font.Tables.emplace(tag, CTableView{offset, length}).second)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt,
					                "SFNT table directory contains a duplicate tag");
				if (length != 0)
					tableRanges.emplace_back(offset, static_cast<std::size_t>(offset) + length);
			}
			std::sort(tableRanges.begin(), tableRanges.end());
			for (std::size_t i = 1; i < tableRanges.size(); ++i)
			{
				if (tableRanges[i].first < tableRanges[i - 1].second)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedSfnt,
					                "SFNT tables overlap each other");
			}

			const std::array<std::uint32_t, 6> variableTables = {
				MakeTag('f', 'v', 'a', 'r'), MakeTag('g', 'v', 'a', 'r'), MakeTag('a', 'v', 'a', 'r'),
				MakeTag('H', 'V', 'A', 'R'), MakeTag('M', 'V', 'A', 'R'), MakeTag('c', 'v', 'a', 'r')};
			for (std::uint32_t tag : variableTables)
			{
				if (font.Tables.find(tag) != font.Tables.end())
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::UnsupportedVariableFont,
					                "variable TrueType fonts are not supported");
			}

			const std::array<std::uint32_t, 6> required = {TagHead, TagHhea, TagMaxp, TagHmtx, TagLoca, TagGlyf};
			for (std::uint32_t tag : required)
			{
				if (font.Tables.find(tag) == font.Tables.end())
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MissingRequiredTable,
					                "required TrueType table is missing");
			}

			const CTableView& head = font.Tables.at(TagHead);
			if (head.Length < 54 || ReadU32(data, head.Offset + 12) != 0x5F0F3CF5u)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHead,
				                "head table is truncated or has an invalid magic number");
			font.UnitsPerEm = ReadU16(data, head.Offset + 18);
			if (font.UnitsPerEm < 16 || font.UnitsPerEm > 16384)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHead,
				                "head.unitsPerEm is outside the TrueType range");
			font.LocaFormat = ReadS16(data, head.Offset + 50);
			if (font.LocaFormat != 0 && font.LocaFormat != 1)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHead,
				                "head.indexToLocFormat is neither short nor long");

			const CTableView& maxp = font.Tables.at(TagMaxp);
			if (maxp.Length < 32 || ReadU32(data, maxp.Offset) != SfntTrueType)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedMaxp,
				                "maxp table is not a complete TrueType maxp table");
			font.NumGlyphs = ReadU16(data, maxp.Offset + 4);
			if (font.NumGlyphs == 0)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedMaxp,
				                "maxp.numGlyphs is zero");

			const CTableView& hhea = font.Tables.at(TagHhea);
			if (hhea.Length < 36 || ReadU32(data, hhea.Offset) != SfntTrueType)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHhea,
				                "hhea table is truncated or has an unsupported version");
			font.NumberOfHMetrics = ReadU16(data, hhea.Offset + 34);
			if (font.NumberOfHMetrics == 0 || font.NumberOfHMetrics > font.NumGlyphs)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHhea,
				                "hhea.numberOfHMetrics is outside the glyph range");

			const CTableView& hmtx = font.Tables.at(TagHmtx);
			const std::size_t hmtxNeeded = static_cast<std::size_t>(font.NumberOfHMetrics) * 4 +
			                               static_cast<std::size_t>(font.NumGlyphs - font.NumberOfHMetrics) * 2;
			if (hmtx.Length < hmtxNeeded)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHmtx,
				                "hmtx table is too short for maxp and hhea");
			font.Advances.resize(font.NumGlyphs);
			font.LeftSideBearings.resize(font.NumGlyphs);
			for (std::uint16_t gid = 0; gid < font.NumberOfHMetrics; ++gid)
			{
				font.Advances[gid] = ReadU16(data, hmtx.Offset + static_cast<std::size_t>(gid) * 4);
				font.LeftSideBearings[gid] = ReadS16(data, hmtx.Offset + static_cast<std::size_t>(gid) * 4 + 2);
			}
			const std::uint16_t repeatedAdvance = font.Advances[font.NumberOfHMetrics - 1];
			const std::size_t bearingsOffset = hmtx.Offset + static_cast<std::size_t>(font.NumberOfHMetrics) * 4;
			for (std::uint16_t gid = font.NumberOfHMetrics; gid < font.NumGlyphs; ++gid)
			{
				font.Advances[gid] = repeatedAdvance;
				font.LeftSideBearings[gid] = ReadS16(data, bearingsOffset +
				                                                static_cast<std::size_t>(gid - font.NumberOfHMetrics) * 2);
			}

			const CTableView& loca = font.Tables.at(TagLoca);
			const std::size_t locaEntrySize = font.LocaFormat == 0 ? 2 : 4;
			const std::size_t locaNeeded = (static_cast<std::size_t>(font.NumGlyphs) + 1) * locaEntrySize;
			if (loca.Length < locaNeeded)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedLoca,
				                "loca table is too short for maxp.numGlyphs");
			font.GlyphOffsets.resize(static_cast<std::size_t>(font.NumGlyphs) + 1);
			const std::size_t glyfLength = font.Tables.at(TagGlyf).Length;
			for (std::size_t i = 0; i <= font.NumGlyphs; ++i)
			{
				const std::uint32_t offset = font.LocaFormat == 0
				                                     ? static_cast<std::uint32_t>(ReadU16(data, loca.Offset + i * 2)) * 2u
				                                     : ReadU32(data, loca.Offset + i * 4);
				if (offset > glyfLength || (i != 0 && offset < font.GlyphOffsets[i - 1]) ||
				    (offset & 1u) != 0)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedLoca,
					                "loca offsets are unordered, unaligned, or outside glyf");
				font.GlyphOffsets[i] = offset;
			}
			return true;
		}

		bool ParseComposite(const std::vector<std::uint8_t>& source,
		                    const CSourceFont& font,
		                    std::uint16_t glyphId,
		                    std::vector<CCompositeReference>& references,
		                    CLogicalTrueTypeSubsetError& error)
		{
			references.clear();
			const CTableView& glyf = font.Tables.at(TagGlyf);
			const std::size_t begin = glyf.Offset + font.GlyphOffsets[glyphId];
			const std::size_t end = glyf.Offset + font.GlyphOffsets[static_cast<std::size_t>(glyphId) + 1];
			if (begin == end)
				return true;
			if (end < begin || end - begin < 10)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
				                "glyph record is shorter than its header", 0, glyphId);
			const std::int16_t contourCount = ReadS16(source, begin);
			if (contourCount >= 0)
			{
				std::size_t cursor = begin + 10;
				const std::size_t endPointsBytes = static_cast<std::size_t>(contourCount) * 2;
				if (!HasRange(end, cursor, endPointsBytes + 2))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "simple glyph contour endpoints are truncated", 0, glyphId);
				std::size_t pointCount = 0;
				for (std::int16_t contour = 0; contour < contourCount; ++contour)
				{
					const std::uint16_t endPoint = ReadU16(source, cursor + static_cast<std::size_t>(contour) * 2);
					if (contour != 0 && static_cast<std::size_t>(endPoint) < pointCount)
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
						                "simple glyph contour endpoints are unordered", 0, glyphId);
					pointCount = static_cast<std::size_t>(endPoint) + 1;
				}
				cursor += endPointsBytes;
				const std::uint16_t instructionLength = ReadU16(source, cursor);
				cursor += 2;
				if (!HasRange(end, cursor, instructionLength))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "simple glyph instructions are truncated", 0, glyphId);
				cursor += instructionLength;

				std::vector<std::uint8_t> flags;
				flags.reserve(pointCount);
				while (flags.size() < pointCount)
				{
					if (!HasRange(end, cursor, 1))
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
						                "simple glyph flags are truncated", 0, glyphId);
					const std::uint8_t flag = source[cursor++];
					std::size_t repetitions = 1;
					if ((flag & 0x08u) != 0)
					{
						if (!HasRange(end, cursor, 1))
							return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
							                "simple glyph flag repeat is truncated", 0, glyphId);
						repetitions += source[cursor++];
					}
					if (repetitions > pointCount - flags.size())
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
						                "simple glyph flag repeat exceeds its point count", 0, glyphId);
					flags.insert(flags.end(), repetitions, flag);
				}
				for (std::uint8_t flag : flags)
				{
					const std::size_t coordinateBytes = (flag & 0x02u) != 0 ? 1 : ((flag & 0x10u) != 0 ? 0 : 2);
					if (!HasRange(end, cursor, coordinateBytes))
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
						                "simple glyph x-coordinates are truncated", 0, glyphId);
					cursor += coordinateBytes;
				}
				for (std::uint8_t flag : flags)
				{
					const std::size_t coordinateBytes = (flag & 0x04u) != 0 ? 1 : ((flag & 0x20u) != 0 ? 0 : 2);
					if (!HasRange(end, cursor, coordinateBytes))
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
						                "simple glyph y-coordinates are truncated", 0, glyphId);
					cursor += coordinateBytes;
				}
				return true;
			}
			if (contourCount != -1)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
				                "glyph uses a reserved negative contour count", 0, glyphId);

			std::size_t cursor = begin + 10;
			std::uint16_t flags = 0;
			do
			{
				if (!HasRange(end, cursor, 4))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph component header is truncated", 0, glyphId);
				flags = ReadU16(source, cursor);
				const std::uint16_t componentGlyph = ReadU16(source, cursor + 2);
				if (componentGlyph >= font.NumGlyphs)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph references an invalid source glyph", 0, glyphId);
				references.push_back({componentGlyph, cursor + 2 - begin});
				cursor += 4;

				const unsigned transformCount = ((flags & HaveScale) != 0 ? 1u : 0u) +
				                                ((flags & HaveXyScale) != 0 ? 1u : 0u) +
				                                ((flags & HaveTwoByTwo) != 0 ? 1u : 0u);
				if (transformCount > 1)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph has conflicting transform flags", 0, glyphId);
				std::size_t componentBytes = (flags & ArgWords) != 0 ? 4 : 2;
				if ((flags & HaveScale) != 0)
					componentBytes += 2;
				else if ((flags & HaveXyScale) != 0)
					componentBytes += 4;
				else if ((flags & HaveTwoByTwo) != 0)
					componentBytes += 8;
				if (!HasRange(end, cursor, componentBytes))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph component data is truncated", 0, glyphId);
				cursor += componentBytes;
			} while ((flags & MoreComponents) != 0);

			if ((flags & HaveInstructions) != 0)
			{
				if (!HasRange(end, cursor, 2))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph instruction length is truncated", 0, glyphId);
				const std::uint16_t instructionLength = ReadU16(source, cursor);
				cursor += 2;
				if (!HasRange(end, cursor, instructionLength))
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph instructions are truncated", 0, glyphId);
			}
			return true;
		}

		bool AddClosure(std::uint16_t glyphId,
		                const std::vector<std::uint8_t>& source,
		                const CSourceFont& font,
		                std::vector<std::uint8_t>& states,
		                std::vector<std::uint16_t>& glyphOrder,
		                std::vector<std::uint32_t>& oldToNew,
		                CLogicalTrueTypeSubsetError& error)
		{
			struct CFrame
			{
				std::uint16_t GlyphId = 0;
				std::vector<CCompositeReference> References;
				std::size_t NextReference = 0;
			};

			if (states[glyphId] == 2)
				return true;

			std::vector<CFrame> stack;
			stack.push_back({glyphId, {}, 0});
			while (!stack.empty())
			{
				CFrame& frame = stack.back();
				if (states[frame.GlyphId] == 0)
				{
					states[frame.GlyphId] = 1;
					if (!ParseComposite(source, font, frame.GlyphId, frame.References, error))
						return false;
				}

				if (frame.NextReference == frame.References.size())
				{
					states[frame.GlyphId] = 2;
					stack.pop_back();
					continue;
				}

				const std::uint16_t dependency = frame.References[frame.NextReference++].GlyphId;
				if (states[dependency] == 1)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::CompositeCycle,
					                "composite glyph dependency cycle detected", 0, dependency);
				if (oldToNew[dependency] == CLogicalTrueTypeSubsetResult::UnmappedGlyph)
				{
					if (glyphOrder.size() >= std::numeric_limits<std::uint16_t>::max())
						return SetError(error, CLogicalTrueTypeSubsetErrorCode::TooManyGlyphs,
						                "compact glyph count exceeds 65535");
					oldToNew[dependency] = static_cast<std::uint32_t>(glyphOrder.size());
					glyphOrder.push_back(dependency);
				}
				if (states[dependency] == 0)
					stack.push_back({dependency, {}, 0});
			}
			return true;
		}

		std::uint32_t TableChecksum(const std::vector<std::uint8_t>& data)
		{
			std::uint32_t sum = 0;
			for (std::size_t i = 0; i < data.size(); i += 4)
			{
				std::uint32_t word = 0;
				for (std::size_t j = 0; j < 4 && i + j < data.size(); ++j)
					word |= static_cast<std::uint32_t>(data[i + j]) << (24 - static_cast<unsigned>(j) * 8);
				sum += word;
			}
			return sum;
		}

		std::vector<std::uint8_t> BuildCmap()
		{
			std::vector<std::uint8_t> cmap;
			cmap.reserve(36);
			AppendU16(cmap, 0);
			AppendU16(cmap, 1);
			AppendU16(cmap, 3);
			AppendU16(cmap, 1);
			AppendU32(cmap, 12);
			AppendU16(cmap, 4);
			AppendU16(cmap, 24);
			AppendU16(cmap, 0);
			AppendU16(cmap, 2);
			AppendU16(cmap, 2);
			AppendU16(cmap, 0);
			AppendU16(cmap, 0);
			AppendU16(cmap, 0xFFFFu);
			AppendU16(cmap, 0);
			AppendU16(cmap, 0xFFFFu);
			AppendU16(cmap, 1);
			AppendU16(cmap, 0);
			return cmap;
		}

		bool EmitFont(std::uint32_t flavor,
		              std::vector<COutputTable>& tables,
		              std::vector<std::uint8_t>& output,
		              CLogicalTrueTypeSubsetError& error)
		{
			std::sort(tables.begin(), tables.end(), [](const COutputTable& left, const COutputTable& right) {
				return left.Tag < right.Tag;
			});
			if (tables.empty() || tables.size() > std::numeric_limits<std::uint16_t>::max())
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
				                "output table count is invalid");

			const std::size_t directorySize = 12 + tables.size() * 16;
			if (directorySize > std::numeric_limits<std::uint32_t>::max())
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
				                "output SFNT directory is too large");
			std::size_t outputSize = directorySize;
			for (COutputTable& table : tables)
			{
				if (outputSize > std::numeric_limits<std::uint32_t>::max() ||
				    table.Data.size() > std::numeric_limits<std::uint32_t>::max() ||
				    table.Data.size() > std::numeric_limits<std::uint32_t>::max() - outputSize)
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
					                "output SFNT exceeds 32-bit table offsets");
				table.Offset = static_cast<std::uint32_t>(outputSize);
				table.Checksum = TableChecksum(table.Data);
				outputSize += (table.Data.size() + 3) & ~static_cast<std::size_t>(3);
			}
			if (outputSize > std::numeric_limits<std::uint32_t>::max())
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
				                "output SFNT exceeds 32-bit table offsets");

			output.assign(outputSize, 0);
			WriteU32(output, 0, flavor);
			const std::uint16_t numTables = static_cast<std::uint16_t>(tables.size());
			WriteU16(output, 4, numTables);
			std::uint16_t maxPower = 1;
			std::uint16_t entrySelector = 0;
			while (static_cast<unsigned>(maxPower) * 2u <= numTables)
			{
				maxPower = static_cast<std::uint16_t>(maxPower * 2);
				++entrySelector;
			}
			WriteU16(output, 6, static_cast<std::uint16_t>(maxPower * 16));
			WriteU16(output, 8, entrySelector);
			WriteU16(output, 10, static_cast<std::uint16_t>(numTables * 16 - maxPower * 16));

			std::size_t headOffset = 0;
			for (std::size_t i = 0; i < tables.size(); ++i)
			{
				const COutputTable& table = tables[i];
				const std::size_t record = 12 + i * 16;
				WriteU32(output, record, table.Tag);
				WriteU32(output, record + 4, table.Checksum);
				WriteU32(output, record + 8, table.Offset);
				WriteU32(output, record + 12, static_cast<std::uint32_t>(table.Data.size()));
				std::copy(table.Data.begin(), table.Data.end(),
				          output.begin() + static_cast<std::ptrdiff_t>(table.Offset));
				if (table.Tag == TagHead)
					headOffset = table.Offset;
			}
			if (headOffset == 0 || !HasRange(output.size(), headOffset, 12))
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedHead,
				                "output head table is missing");
			WriteU32(output, headOffset + 8, ChecksumMagic - TableChecksum(output));
			return true;
		}
	}

	bool TryBuildSourceBackedLogicalTrueType(const std::vector<std::uint8_t>& source,
	                                         const CLogicalFontShard& shard,
	                                         CLogicalTrueTypeSubsetResult& result,
	                                         CLogicalTrueTypeSubsetError& error)
	{
		result = CLogicalTrueTypeSubsetResult();
		error = CLogicalTrueTypeSubsetError();

		CSourceFont font;
		if (!ParseSourceFont(source, font, error))
			return false;
		if (shard.GetVisualCount() > std::numeric_limits<std::uint16_t>::max() ||
		    shard.GetSemanticCount() > std::numeric_limits<std::uint16_t>::max())
			return SetError(error, CLogicalTrueTypeSubsetErrorCode::TooManyGlyphs,
			                "logical shard contains more than 65535 nonzero IDs");

		result.UnitsPerEm = font.UnitsPerEm;
		result.SourceGidToSubsetGid.assign(font.NumGlyphs, CLogicalTrueTypeSubsetResult::UnmappedGlyph);
		result.VisualRecordToSubsetGid.assign(shard.GetVisualCount() + 1, 0);
		result.CidToSubsetGid.assign(shard.GetSemanticCount() + 1, 0);
		std::vector<std::uint16_t> glyphOrder;
		glyphOrder.push_back(0);
		result.SourceGidToSubsetGid[0] = 0;

		for (std::size_t id = 1; id <= shard.GetVisualCount(); ++id)
		{
			const TLogicalVisualRecordId visualId = static_cast<TLogicalVisualRecordId>(id);
			const CLogicalVisualRecord* record = shard.GetVisualRecord(visualId);
			if (record == nullptr || record->Id != visualId)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::InvalidVisualRecord,
				                "visual record ID is missing or inconsistent", visualId);
			if (record->Visual.Components.size() != 1 || record->Visual.Components[0].X != 0 ||
			    record->Visual.Components[0].Y != 0)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::UnsupportedVisual,
				                "source-backed visuals require exactly one unshifted component", visualId);
			const std::uint32_t sourceGid = record->Visual.Components[0].GlyphId;
			if (sourceGid == 0 || sourceGid >= font.NumGlyphs)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::InvalidSourceGlyph,
				                "visual source glyph must be between 1 and maxp.numGlyphs minus one",
				                visualId, sourceGid);
			if (record->Visual.AdvanceWidth != static_cast<int>(font.Advances[sourceGid]))
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::AdvanceWidthMismatch,
				                "visual advance does not equal the nominal source hmtx advance", visualId, sourceGid);

			if (result.SourceGidToSubsetGid[sourceGid] == CLogicalTrueTypeSubsetResult::UnmappedGlyph)
			{
				if (glyphOrder.size() >= std::numeric_limits<std::uint16_t>::max())
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::TooManyGlyphs,
					                "compact glyph count exceeds 65535", visualId, sourceGid);
				result.SourceGidToSubsetGid[sourceGid] = static_cast<std::uint32_t>(glyphOrder.size());
				glyphOrder.push_back(static_cast<std::uint16_t>(sourceGid));
			}
			result.VisualRecordToSubsetGid[id] = result.SourceGidToSubsetGid[sourceGid];
		}

		std::vector<std::uint8_t> states(font.NumGlyphs, 0);
		for (std::size_t i = 0; i < glyphOrder.size(); ++i)
		{
			if (!AddClosure(glyphOrder[i], source, font, states, glyphOrder,
			                result.SourceGidToSubsetGid, error))
				return false;
		}

		for (std::size_t cid = 1; cid <= shard.GetSemanticCount(); ++cid)
		{
			const CLogicalCidRecord* record = shard.GetCidRecord(static_cast<TLogicalCid>(cid));
			if (record == nullptr || record->Cid != cid || record->VisualRecordId == 0 ||
			    record->VisualRecordId > shard.GetVisualCount())
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::InvalidCidRecord,
				                "CID record is missing or references an invalid visual record");
			const CLogicalVisualRecord* visual = shard.GetVisualRecord(record->VisualRecordId);
			if (visual == nullptr || record->Width != visual->Visual.AdvanceWidth)
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::InvalidCidRecord,
				                "CID width is inconsistent with its visual record", record->VisualRecordId);
			result.CidToSubsetGid[cid] = result.VisualRecordToSubsetGid[record->VisualRecordId];
		}

		std::vector<std::uint8_t> glyf;
		std::vector<std::uint8_t> loca;
		std::vector<std::uint8_t> hmtx;
		loca.reserve((glyphOrder.size() + 1) * 4);
		hmtx.reserve(glyphOrder.size() * 4);
		const CTableView& sourceGlyf = font.Tables.at(TagGlyf);
		for (std::uint16_t oldGid : glyphOrder)
		{
			if (glyf.size() > std::numeric_limits<std::uint32_t>::max())
				return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
				                "rebuilt glyf table exceeds long loca offsets");
			AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));
			const std::size_t oldBegin = sourceGlyf.Offset + font.GlyphOffsets[oldGid];
			const std::size_t oldEnd = sourceGlyf.Offset + font.GlyphOffsets[static_cast<std::size_t>(oldGid) + 1];
			std::vector<std::uint8_t> glyph(source.begin() + static_cast<std::ptrdiff_t>(oldBegin),
			                                source.begin() + static_cast<std::ptrdiff_t>(oldEnd));
			std::vector<CCompositeReference> references;
			if (!ParseComposite(source, font, oldGid, references, error))
				return false;
			for (const CCompositeReference& reference : references)
			{
				const std::uint32_t newGid = result.SourceGidToSubsetGid[reference.GlyphId];
				if (newGid == CLogicalTrueTypeSubsetResult::UnmappedGlyph ||
				    newGid > std::numeric_limits<std::uint16_t>::max())
					return SetError(error, CLogicalTrueTypeSubsetErrorCode::MalformedGlyf,
					                "composite glyph closure is incomplete", 0, oldGid);
				WriteU16(glyph, reference.GlyphIdOffset, static_cast<std::uint16_t>(newGid));
			}
			glyf.insert(glyf.end(), glyph.begin(), glyph.end());
			AppendU16(hmtx, font.Advances[oldGid]);
			AppendU16(hmtx, static_cast<std::uint16_t>(font.LeftSideBearings[oldGid]));
		}
		if (glyf.size() > std::numeric_limits<std::uint32_t>::max())
			return SetError(error, CLogicalTrueTypeSubsetErrorCode::OutputTooLarge,
			                "rebuilt glyf table exceeds long loca offsets");
		AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));

		std::vector<std::uint8_t> head = CopyTable(source, font.Tables.at(TagHead));
		WriteU32(head, 8, 0);
		WriteU16(head, 50, 1);
		std::vector<std::uint8_t> hhea = CopyTable(source, font.Tables.at(TagHhea));
		WriteU16(hhea, 34, static_cast<std::uint16_t>(glyphOrder.size()));
		std::vector<std::uint8_t> maxp = CopyTable(source, font.Tables.at(TagMaxp));
		WriteU16(maxp, 4, static_cast<std::uint16_t>(glyphOrder.size()));
		std::vector<std::uint8_t> post(32, 0);
		const auto sourcePost = font.Tables.find(TagPost);
		if (sourcePost != font.Tables.end() && sourcePost->second.Length >= post.size())
			std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(sourcePost->second.Offset),
			            post.size(), post.begin());
		WriteU32(post, 0, 0x00030000u);

		std::vector<COutputTable> tables;
		tables.push_back({TagCmap, BuildCmap()});
		tables.push_back({TagGlyf, std::move(glyf)});
		tables.push_back({TagHead, std::move(head)});
		tables.push_back({TagHhea, std::move(hhea)});
		tables.push_back({TagHmtx, std::move(hmtx)});
		tables.push_back({TagLoca, std::move(loca)});
		tables.push_back({TagMaxp, std::move(maxp)});
		tables.push_back({TagPost, std::move(post)});

		const std::array<std::uint32_t, 6> safeTables = {
			MakeTag('n', 'a', 'm', 'e'), MakeTag('O', 'S', '/', '2'), MakeTag('c', 'v', 't', ' '),
			MakeTag('f', 'p', 'g', 'm'), MakeTag('p', 'r', 'e', 'p'), MakeTag('g', 'a', 's', 'p')};
		for (std::uint32_t tag : safeTables)
		{
			const auto found = font.Tables.find(tag);
			if (found != font.Tables.end())
				tables.push_back({tag, CopyTable(source, found->second)});
		}

		if (!EmitFont(font.Flavor, tables, result.FontData, error))
		{
			result = CLogicalTrueTypeSubsetResult();
			return false;
		}
		return true;
	}

	bool TryGetTrueTypeGlyphAdvance(const std::vector<std::uint8_t>& fontData,
	                                std::uint32_t glyphId,
	                                std::uint16_t& advanceWidth,
	                                CLogicalTrueTypeSubsetError& error)
	{
		error = CLogicalTrueTypeSubsetError();
		CSourceFont font;
		if (!ParseSourceFont(fontData, font, error))
			return false;
		if (glyphId >= font.NumGlyphs)
			return SetError(error, CLogicalTrueTypeSubsetErrorCode::InvalidSourceGlyph,
			                "glyph ID is outside maxp.numGlyphs", 0, glyphId);
		advanceWidth = font.Advances[glyphId];
		return true;
	}
}
