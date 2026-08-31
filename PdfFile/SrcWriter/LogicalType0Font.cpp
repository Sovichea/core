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
#include "LogicalType0Font.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		constexpr std::size_t MaxBfCharEntries = 100;
		constexpr std::size_t MaxCid = std::numeric_limits<std::uint16_t>::max();
		constexpr char HexDigits[] = "0123456789ABCDEF";

		bool SetError(CLogicalType0FontError& error,
		              CLogicalType0FontErrorCode code,
		              const std::string& message,
		              TLogicalCid cid = CLogicalType0FontError::NoCid,
		              std::size_t textIndex = CLogicalType0FontError::NoTextIndex)
		{
			error.Code = code;
			error.Cid = cid;
			error.TextIndex = textIndex;
			error.Message = message;
			return false;
		}

		void AppendHexU16(std::string& output, std::uint16_t value)
		{
			output.push_back(HexDigits[(value >> 12) & 0x0Fu]);
			output.push_back(HexDigits[(value >> 8) & 0x0Fu]);
			output.push_back(HexDigits[(value >> 4) & 0x0Fu]);
			output.push_back(HexDigits[value & 0x0Fu]);
		}

		bool IsUnicodeScalar(char32_t value)
		{
			const std::uint32_t scalar = static_cast<std::uint32_t>(value);
			return scalar <= 0x10FFFFu && (scalar < 0xD800u || scalar > 0xDFFFu);
		}

		void AppendScalarUtf16BeHex(std::string& output, char32_t value)
		{
			const std::uint32_t scalar = static_cast<std::uint32_t>(value);
			if (scalar <= 0xFFFFu)
			{
				AppendHexU16(output, static_cast<std::uint16_t>(scalar));
				return;
			}

			const std::uint32_t supplementary = scalar - 0x10000u;
			AppendHexU16(output, static_cast<std::uint16_t>(0xD800u + (supplementary >> 10)));
			AppendHexU16(output, static_cast<std::uint16_t>(0xDC00u + (supplementary & 0x3FFu)));
		}

		bool ValidateShard(const CLogicalFontShard& shard,
		                   std::vector<const CLogicalCidRecord*>& records,
		                   CLogicalType0FontError& error)
		{
			const std::size_t cidCount = shard.GetSemanticCount();
			const std::size_t visualCount = shard.GetVisualCount();
			if (cidCount > MaxCid)
				return SetError(error, CLogicalType0FontErrorCode::TooManyCids,
				                "logical Type0 font has more than 65535 nonzero CIDs");
			if (visualCount > MaxCid)
				return SetError(error, CLogicalType0FontErrorCode::InvalidVisualRecord,
				                "logical Type0 font has more than 65535 visual record IDs");

			records.assign(cidCount + 1, nullptr);
			for (std::size_t index = 1; index <= cidCount; ++index)
			{
				const TLogicalCid cid = static_cast<TLogicalCid>(index);
				const CLogicalCidRecord* record = shard.GetCidRecord(cid);
				if (record == nullptr)
					return SetError(error, CLogicalType0FontErrorCode::InvalidCidRecord,
					                "logical CID record is missing", cid);
				if (record->Cid != cid)
					return SetError(error, CLogicalType0FontErrorCode::InvalidCidRecord,
					                "logical CID record ID does not match its index", cid);
				if (record->VisualRecordId == 0 || record->VisualRecordId > visualCount)
					return SetError(error, CLogicalType0FontErrorCode::InvalidVisualRecord,
					                "logical CID references a visual record outside the shard", cid);
				if (record->Text.empty())
					return SetError(error, CLogicalType0FontErrorCode::InvalidCidRecord,
					                "logical CID text must contain at least one Unicode scalar", cid);

				const CLogicalVisualRecord* visual = shard.GetVisualRecord(record->VisualRecordId);
				if (visual == nullptr || visual->Id != record->VisualRecordId)
					return SetError(error, CLogicalType0FontErrorCode::InvalidVisualRecord,
					                "logical CID references a missing or misindexed visual record", cid);

				for (std::size_t textIndex = 0; textIndex < record->Text.size(); ++textIndex)
				{
					if (!IsUnicodeScalar(record->Text[textIndex]))
						return SetError(error, CLogicalType0FontErrorCode::InvalidUnicodeScalar,
						                "logical CID text contains a surrogate or a value above U+10FFFF",
						                cid, textIndex);
				}
				records[index] = record;
			}
			return true;
		}

		bool BuildToUnicode(const std::vector<const CLogicalCidRecord*>& records,
		                    std::string& output,
		                    CLogicalType0FontError& error)
		{
			const std::size_t cidCount = records.size() - 1;
			std::size_t utf16CodeUnits = 0;
			for (std::size_t cid = 1; cid <= cidCount; ++cid)
			{
				for (char32_t scalar : records[cid]->Text)
				{
					const std::size_t units = static_cast<std::uint32_t>(scalar) <= 0xFFFFu ? 1 : 2;
					if (utf16CodeUnits > std::numeric_limits<std::size_t>::max() - units)
						return SetError(error, CLogicalType0FontErrorCode::OutputTooLarge,
						                "ToUnicode UTF-16 length exceeds addressable memory",
						                static_cast<TLogicalCid>(cid));
					utf16CodeUnits += units;
				}
			}

			const std::size_t fixedSize = 320;
			const std::size_t perCidSize = 14;
			if (cidCount > (std::numeric_limits<std::size_t>::max() - fixedSize) / perCidSize ||
			    utf16CodeUnits > (std::numeric_limits<std::size_t>::max() - fixedSize - cidCount * perCidSize) / 4)
				return SetError(error, CLogicalType0FontErrorCode::OutputTooLarge,
				                "ToUnicode CMap size exceeds addressable memory");

			output.clear();
			output.reserve(fixedSize + cidCount * perCidSize + utf16CodeUnits * 4);
			output += "/CIDInit /ProcSet findresource begin\n"
			          "12 dict begin\n"
			          "begincmap\n"
			          "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
			          "/CMapName /Adobe-Identity-UCS def\n"
			          "/CMapType 2 def\n"
			          "1 begincodespacerange\n"
			          "<0000> <FFFF>\n"
			          "endcodespacerange\n";

			for (std::size_t blockStart = 1; blockStart <= cidCount; blockStart += MaxBfCharEntries)
			{
				const std::size_t blockSize = std::min(MaxBfCharEntries, cidCount - blockStart + 1);
				output += std::to_string(blockSize);
				output += " beginbfchar\n";
				for (std::size_t offset = 0; offset < blockSize; ++offset)
				{
					const std::size_t cid = blockStart + offset;
					output.push_back('<');
					AppendHexU16(output, static_cast<std::uint16_t>(cid));
					output += "> <";
					for (char32_t scalar : records[cid]->Text)
						AppendScalarUtf16BeHex(output, scalar);
					output += ">\n";
				}
				output += "endbfchar\n";
			}

			output += "endcmap\n"
			          "CMapName currentdict /CMap defineresource pop\n"
			          "end\n"
			          "end\n";
			return true;
		}
	}

	bool TryBuildLogicalType0Font(const std::vector<std::uint8_t>& source,
	                                  const CLogicalFontShard& shard,
	                                  CLogicalType0FontResult& result,
	                                  CLogicalType0FontError& error,
	                                  const std::string& fontName)
	{
		error = CLogicalType0FontError();
		std::vector<const CLogicalCidRecord*> records;
		if (!ValidateShard(shard, records, error))
			return false;

		CLogicalTrueTypeSubsetResult subset;
		CLogicalTrueTypeSubsetError subsetError;
		if (!TryBuildLogicalTrueType(source, shard, subset, subsetError, fontName))
		{
			error.SubsetError = subsetError;
			return SetError(error, CLogicalType0FontErrorCode::SubsetBuildFailed,
			                "failed to build compact FontFile2 subset: " + subsetError.Message);
		}

		const std::size_t cidCount = shard.GetSemanticCount();
		if (subset.CidToSubsetGid.size() != cidCount + 1)
			return SetError(error, CLogicalType0FontErrorCode::InvalidSubsetResult,
			                "compact subset returned a CID-to-GID table with the wrong size");
		if (subset.CidToSubsetGid[0] != 0)
			return SetError(error, CLogicalType0FontErrorCode::InvalidSubsetResult,
			                "compact subset mapped reserved CID 0 to a nonzero glyph");

		CLogicalType0FontResult built;
		built.FontFile2 = std::move(subset.FontData);
		built.CIDToGIDMap.assign((cidCount + 1) * 2, 0);
		built.Widths.assign(cidCount + 1, 0);
		for (std::size_t index = 1; index <= cidCount; ++index)
		{
			const std::uint32_t gid = subset.CidToSubsetGid[index];
			if (gid == CLogicalTrueTypeSubsetResult::UnmappedGlyph || gid == 0 || gid > MaxCid)
				return SetError(error, CLogicalType0FontErrorCode::InvalidSubsetResult,
				                "compact subset returned an invalid 16-bit GID for logical CID",
				                static_cast<TLogicalCid>(index));
			built.CIDToGIDMap[index * 2] = static_cast<std::uint8_t>(gid >> 8);
			built.CIDToGIDMap[index * 2 + 1] = static_cast<std::uint8_t>(gid);
			const std::int64_t sourceWidth = records[index]->Width;
			built.Widths[index] = static_cast<int>(
				(sourceWidth * 1000 + subset.UnitsPerEm / 2) / subset.UnitsPerEm);
		}

		if (!BuildToUnicode(records, built.ToUnicode, error))
			return false;

		result = std::move(built);
		return true;
	}

}
